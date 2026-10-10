#include "ai/AiRefine.h"

#include "ai/AiExchange.h"
#include "ai/PatternCompact.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/VelocityContour.h"

#include <algorithm>
#include <map>
#include <set>

namespace mm::ai {

namespace {

using namespace mm::core;

const char* roleName(VoiceRole role) {
    return role == VoiceRole::Bass ? "bass" : "melody";
}

/// Everything the schema can say about a note; the rest (ticks off the grid, v1.1 fields, locks) comes from the
/// original.
bool sameInSchema(const Note& a, const Note& b) {
    return a.pitch == b.pitch && a.startTick == b.startTick && a.lengthTicks == b.lengthTicks &&
           a.velocity == b.velocity && a.accent == b.accent && a.slide == b.slide;
}

struct Window {
    uint32_t from = 0;
    uint32_t to = 0xFFFFFFFFu;
    bool contains(uint32_t tick) const { return tick >= from && tick < to; }
};

Window windowOf(const Pattern& pattern, std::optional<size_t> phrase) {
    Window window;
    if (phrase && *phrase < pattern.phrases.size()) {
        const auto& entry = pattern.phrases[*phrase];
        window.from = entry.startBar * kTicksPerBar;
        window.to = (entry.startBar + entry.lengthBars) * kTicksPerBar;
    }
    return window;
}

} // namespace

Pattern mergeRefinement(const Pattern& current, const Pattern& rounded, const Pattern& answer,
                        std::optional<size_t> voice, std::optional<size_t> phrase) {
    Pattern result = current;
    const Window window = windowOf(current, phrase);
    uint32_t next = std::max(current.nextNoteId, answer.nextNoteId);
    for (size_t i = 0; i < result.voices.size(); ++i) {
        const bool inScope = !voice || *voice == i;
        if (!inScope || isVoiceLocked(current.voices[i]) || i >= answer.voices.size() || i >= rounded.voices.size()) {
            continue; // stays exactly as it is
        }
        std::map<uint32_t, const Note*> originals;
        std::map<uint32_t, const Note*> roundedById;
        for (const auto& note : current.voices[i].notes) {
            originals[note.id] = &note;
        }
        for (const auto& note : rounded.voices[i].notes) {
            roundedById[note.id] = &note;
        }
        std::vector<Note> notes;
        std::set<uint32_t> used;
        // Notes outside the phrase are not the answer's business: they stay.
        for (const auto& note : current.voices[i].notes) {
            if (!window.contains(note.startTick)) {
                notes.push_back(note);
                used.insert(note.id);
            }
        }
        for (const auto& note : answer.voices[i].notes) {
            if (!window.contains(note.startTick)) {
                continue;
            }
            const auto known = roundedById.find(note.id);
            // `used` holds the ids of the notes that stay outside the phrase, so an id of one of them is a new note
            // here.
            const bool keepsId = known != roundedById.end() && used.insert(note.id).second;
            if (!keepsId) {
                Note added = note;
                added.id = next++;
                notes.push_back(added);
            } else if (sameInSchema(note, *known->second)) {
                notes.push_back(*originals.at(note.id)); // unchanged: the original, off-grid ticks included
            } else {
                Note changed = *originals.at(note.id);
                changed.pitch = note.pitch;
                changed.startTick = note.startTick;
                changed.lengthTicks = note.lengthTicks;
                changed.velocity = note.velocity;
                changed.accent = note.accent;
                changed.slide = note.slide;
                notes.push_back(changed);
            }
        }
        std::stable_sort(notes.begin(), notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        result.voices[i].notes = std::move(notes);
    }
    result.nextNoteId = next;
    return result;
}

AiGenerateResult refineWithAi(IAiProvider& provider, const AiRefineInput& input, const CancellationToken& token) {
    AiGenerateResult result;
    if (input.style == nullptr || input.templates == nullptr) {
        result.error = "no style or no prompt templates";
        return result;
    }
    const Pattern& current = input.pattern;
    const std::optional<size_t> phrase = input.voice ? std::nullopt : input.phrase;

    RefinePromptInput promptInput;
    promptInput.style = input.style;
    promptInput.pattern = &current;
    promptInput.instruction = input.instruction;
    promptInput.history = current.refineHistory;
    promptInput.voice = input.voice;
    promptInput.phrase = phrase;
    promptInput.energyPct = input.energyPct;
    promptInput.creativityPct = input.creativityPct;
    const auto prompt = buildRefinePrompt(*input.templates, promptInput);
    const auto compact = patternToSchemaJson(current, input.style, true);
    const auto symbols = progressionSymbols(current);
    if (!prompt || !compact || !symbols) {
        result.error = "the prompt could not be built";
        return result;
    }

    std::set<uint32_t> knownIds;
    for (const auto& track : current.voices) {
        for (const auto& note : track.notes) {
            knownIds.insert(note.id);
        }
    }
    const std::string providerId = provider.info().id;
    BuildContext context;
    context.lengthBars = current.lengthBars;
    context.styleId = input.style->id;
    context.style = input.style;
    context.energyPct = input.energyPct;
    context.creativityPct = input.creativityPct;
    context.prompt = input.instruction;
    context.promptVersion = kPromptVersion;
    context.providerId = providerId;
    context.modelId = input.model;
    context.createdUnixMs = input.createdUnixMs;
    context.knownIds = &knownIds;
    context.firstNewId = current.nextNoteId;

    // The current pattern as the schema carries it: the baseline for "unchanged", and the stand-in for a voice that the
    // answer leaves out.
    const auto roundedParsed = parseAiResponse(compact->json);
    if (!roundedParsed.ok) {
        result.error = "the current pattern cannot be written in the schema";
        return result;
    }
    BuildContext roundedContext = context;
    const auto roundedBuilt = buildPattern(roundedParsed.draft, roundedContext);
    if (!roundedBuilt.ok) {
        result.error = "the current pattern cannot be written in the schema: " + roundedBuilt.error;
        return result;
    }

    const auto makeAttempt = [&](const std::string& text) {
        ExchangeAttempt attempt;
        const auto parsed = parseAiResponse(text);
        if (!parsed.ok) {
            attempt.error = parsed.error;
            return attempt;
        }
        AiDraft draft = parsed.draft;
        // The harmony stays whatever the answer says; a voice the answer leaves out is the current one.
        draft.root = rootName(current.context.root);
        draft.scale = current.context.scaleId;
        draft.progression = *symbols;
        for (const auto& voice : roundedParsed.draft.voices) {
            if (std::none_of(draft.voices.begin(), draft.voices.end(),
                             [&](const AiVoiceDraft& v) { return v.role == voice.role; })) {
                draft.voices.push_back(voice);
            }
        }
        BuildContext answerContext = context;
        answerContext.rawResponse = text;
        const auto built = buildPattern(draft, answerContext);
        attempt.droppedNotes = built.droppedNotes;
        attempt.clampedValues = built.clampedValues;
        if (!built.ok) {
            attempt.error = built.error;
            return attempt;
        }
        Pattern pattern = mergeRefinement(current, roundedBuilt.pattern, built.pattern, input.voice, phrase);
        pattern.info.source = "refine";
        pattern.info.providerId = providerId;
        pattern.info.modelId = input.model;
        pattern.info.promptVersion = kPromptVersion;
        pattern.info.rawResponse = text;
        pattern.info.createdUnixMs = input.createdUnixMs;
        pattern.info.energyPct = built.pattern.info.energyPct;
        pattern.info.creativityPct = built.pattern.info.creativityPct;
        pattern.refineHistory = current.refineHistory;
        pattern.refineHistory.push_back(input.instruction);
        if (pattern.refineHistory.size() > kRefineHistorySize) {
            pattern.refineHistory.erase(pattern.refineHistory.begin(),
                                        pattern.refineHistory.end() - static_cast<std::ptrdiff_t>(kRefineHistorySize));
        }
        // A note that is off the grid (played, imported, drawn with a finer grid) is a note the musician kept: the
        // constraint layer must not snap it, so it counts as rhythm-locked while the layer runs.
        std::vector<std::pair<size_t, uint32_t>> held;
        for (size_t i = 0; i < pattern.voices.size(); ++i) {
            for (auto& note : pattern.voices[i].notes) {
                if ((note.startTick % kTicksPerStep != 0 || note.lengthTicks % kTicksPerStep != 0) &&
                    !note.lock.rhythm) {
                    note.lock.rhythm = true;
                    held.emplace_back(i, note.id);
                }
            }
        }
        applyConstraints(pattern, constraintSettingsFor(pattern, *input.style));
        for (const auto& [voiceIndex, id] : held) {
            for (auto& note : pattern.voices[voiceIndex].notes) {
                if (note.id == id) {
                    note.lock.rhythm = false;
                }
            }
        }
        for (size_t i = 0; i < pattern.voices.size(); ++i) {
            const bool touched = (!input.voice || *input.voice == i) && !isVoiceLocked(pattern.voices[i]);
            if (touched && pattern.voices[i].notes.empty()) {
                attempt.error = std::string("voices: the ") + roleName(pattern.voices[i].role) + " voice has no notes";
                return attempt;
            }
        }
        attempt.pattern = std::move(pattern);
        attempt.ok = true;
        return attempt;
    };

    AiRequest request;
    request.model = input.model;
    request.schemaJson = schemaV1Json();
    request.temperature = std::clamp(input.creativityPct, 0, 100) / 100.0;
    request.maxTokens = input.maxTokens;
    request.timeoutSeconds = input.timeoutSeconds;
    return runExchange(provider, *prompt, request, *input.style, input.energyPct, input.creativityPct, makeAttempt,
                       token);
}

} // namespace mm::ai
