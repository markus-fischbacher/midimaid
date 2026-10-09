#include "ai/AiSchema.h"

#include "core/Archetype.h"
#include "core/ChordSymbol.h"
#include "core/FormPlan.h"
#include "core/PatternValidation.h"
#include "core/Register.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>

namespace mm::ai {

namespace {

using nlohmann::json;

constexpr int kStepsPerBar = 16;
constexpr uint32_t kTicksPerStep = mm::core::kTicksPerBar / kStepsPerBar;

/// The deepest nesting of [ and { outside of strings.
int maxDepth(std::string_view text) {
    int depth = 0;
    int deepest = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            deepest = std::max(deepest, ++depth);
        } else if (c == ']' || c == '}') {
            depth = std::max(depth - 1, 0);
        }
    }
    return deepest;
}

/// An integer from a JSON number (a fractional number is rounded); false for anything else.
bool readInt(const json& value, int& out) {
    if (value.is_number_integer()) {
        const auto raw =
            value.is_number_unsigned()
                ? static_cast<long long>(std::min<uint64_t>(value.get<uint64_t>(), std::numeric_limits<int>::max()))
                : value.get<long long>();
        out = static_cast<int>(
            std::clamp<long long>(raw, std::numeric_limits<int>::min() / 2, std::numeric_limits<int>::max() / 2));
        return true;
    }
    if (value.is_number_float()) {
        const double raw = value.get<double>();
        if (!std::isfinite(raw)) {
            return false;
        }
        out = static_cast<int>(std::clamp(std::llround(std::clamp(raw, -1e9, 1e9)), -1000000000LL, 1000000000LL));
        return true;
    }
    return false;
}

std::optional<json> parseJsonText(std::string_view text) {
    json root = json::parse(text.begin(), text.end(), nullptr, false);
    if (!root.is_discarded()) {
        return root;
    }
    const auto first = text.find('{');
    const auto last = text.rfind('}');
    if (first == std::string_view::npos || last == std::string_view::npos || last <= first) {
        return std::nullopt;
    }
    const auto inner = text.substr(first, last - first + 1);
    root = json::parse(inner.begin(), inner.end(), nullptr, false);
    if (root.is_discarded()) {
        return std::nullopt;
    }
    return root;
}

ParseResult fail(std::string error) {
    ParseResult result;
    result.error = std::move(error);
    return result;
}

std::string stringOr(const json& object, const char* key, std::string fallback = {}) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::move(fallback);
}

void readNote(const json& value, AiVoiceDraft& voice, size_t& dropped) {
    int step = 0;
    int degree = 0;
    if (!value.is_object()) {
        ++dropped;
        return;
    }
    const auto stepIt = value.find("step");
    const auto degreeIt = value.find("degree");
    if (stepIt == value.end() || degreeIt == value.end() || !readInt(*stepIt, step) || !readInt(*degreeIt, degree)) {
        ++dropped;
        return;
    }
    AiNoteDraft note;
    note.step = step;
    note.degree = degree;
    const auto optionalInt = [&](const char* key, int& out) {
        if (const auto it = value.find(key); it != value.end()) {
            int parsed = 0;
            if (readInt(*it, parsed)) {
                out = parsed;
            }
        }
    };
    optionalInt("alt", note.alt);
    optionalInt("octave", note.octave);
    optionalInt("len", note.len);
    optionalInt("vel", note.vel);
    if (const auto it = value.find("id"); it != value.end()) {
        int id = 0;
        if (readInt(*it, id) && id > 0) {
            note.id = static_cast<uint32_t>(id);
        }
    }
    if (const auto it = value.find("accent"); it != value.end() && it->is_boolean()) {
        note.accent = it->get<bool>();
    }
    if (const auto it = value.find("slide"); it != value.end() && it->is_boolean()) {
        note.slide = it->get<bool>();
    }
    voice.notes.push_back(note);
}

} // namespace

std::string schemaV1Json() {
    return R"JSON({
  "type": "object",
  "required": ["schema_version", "context", "voices"],
  "properties": {
    "schema_version": {"type": "integer", "const": 1},
    "intent": {
      "type": "object",
      "properties": {
        "energy": {"type": "number", "minimum": 0, "maximum": 1},
        "density": {"type": "number", "minimum": 0, "maximum": 1},
        "contour": {"type": "string", "enum": ["rising", "falling", "arch", "static", "wave"]},
        "motif_idea": {"type": "string"},
        "groove": {"type": "string", "enum": ["straight", "swing", "offbeat", "rolling"]}
      }
    },
    "context": {
      "type": "object",
      "required": ["root", "scale", "progression"],
      "properties": {
        "root": {"type": "string"},
        "scale": {"type": "string"},
        "progression": {"type": "array", "items": {"type": "string"}, "minItems": 1}
      }
    },
    "phrases": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["start_bar", "bars", "role"],
        "properties": {
          "start_bar": {"type": "integer", "minimum": 0},
          "bars": {"type": "integer", "minimum": 1},
          "role": {"type": "string", "enum": ["main", "variation", "build", "breakdown", "answer"]},
          "turnaround": {"type": "boolean"}
        }
      }
    },
    "voices": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["role", "notes"],
        "properties": {
          "role": {"type": "string", "enum": ["bass", "melody"]},
          "archetype": {"type": "string"},
          "motif_bars": {"type": "integer", "minimum": 1},
          "notes": {
            "type": "array",
            "items": {
              "type": "object",
              "required": ["step", "degree"],
              "properties": {
                "id": {"type": "integer", "minimum": 1},
                "step": {"type": "integer", "minimum": 0},
                "degree": {"type": "integer", "minimum": 1},
                "alt": {"type": "integer", "minimum": -1, "maximum": 1},
                "octave": {"type": "integer"},
                "len": {"type": "integer", "minimum": 1},
                "vel": {"type": "integer", "minimum": 1, "maximum": 127},
                "accent": {"type": "boolean"},
                "slide": {"type": "boolean"}
              }
            }
          }
        }
      }
    }
  }
})JSON";
}

ParseResult parseAiResponse(std::string_view text) {
    if (text.size() > kMaxResponseBytes) {
        return fail("response: too large");
    }
    if (maxDepth(text) > kMaxJsonDepth) {
        return fail("response: nested too deeply");
    }
    const auto parsed = parseJsonText(text);
    if (!parsed) {
        return fail("response: no valid JSON");
    }
    const json& root = *parsed;
    if (!root.is_object()) {
        return fail("response: expected an object");
    }

    ParseResult result;
    AiDraft& draft = result.draft;
    const auto version = root.find("schema_version");
    if (version == root.end() || !version->is_number_integer() || version->get<long long>() < 1) {
        return fail("schema_version: missing or not a positive integer");
    }
    draft.schemaVersion = static_cast<int>(std::min<long long>(version->get<long long>(), 1000000));

    if (const auto intent = root.find("intent"); intent != root.end() && intent->is_object()) {
        if (const auto it = intent->find("energy"); it != intent->end() && it->is_number()) {
            draft.intent.energy = it->get<double>();
        }
        if (const auto it = intent->find("density"); it != intent->end() && it->is_number()) {
            draft.intent.density = it->get<double>();
        }
        draft.intent.contour = stringOr(*intent, "contour");
        draft.intent.motifIdea = stringOr(*intent, "motif_idea");
        draft.intent.groove = stringOr(*intent, "groove");
    }

    const auto context = root.find("context");
    if (context == root.end() || !context->is_object()) {
        return fail("context: missing or not an object");
    }
    const auto rootName = context->find("root");
    if (rootName == context->end() || !rootName->is_string()) {
        return fail("context.root: missing or not a string");
    }
    draft.root = rootName->get<std::string>();
    const auto scale = context->find("scale");
    if (scale == context->end() || !scale->is_string()) {
        return fail("context.scale: missing or not a string");
    }
    draft.scale = scale->get<std::string>();
    const auto progression = context->find("progression");
    if (progression == context->end() || !progression->is_array() || progression->empty()) {
        return fail("context.progression: missing or not a non-empty array");
    }
    if (progression->size() > kMaxProgressionEntries) {
        return fail("context.progression: too many entries");
    }
    for (size_t i = 0; i < progression->size(); ++i) {
        if (!(*progression)[i].is_string()) {
            return fail("context.progression[" + std::to_string(i) + "]: expected a string");
        }
        draft.progression.push_back((*progression)[i].get<std::string>());
    }

    if (const auto phrases = root.find("phrases");
        phrases != root.end() && phrases->is_array() && phrases->size() <= kMaxPhrases) {
        bool usable = true;
        std::vector<AiPhraseDraft> list;
        for (const auto& entry : *phrases) {
            AiPhraseDraft phrase;
            const auto start = entry.is_object() ? entry.find("start_bar") : entry.end();
            const auto bars = entry.is_object() ? entry.find("bars") : entry.end();
            if (!entry.is_object() || start == entry.end() || bars == entry.end() ||
                !readInt(*start, phrase.startBar) || !readInt(*bars, phrase.bars)) {
                usable = false;
                break;
            }
            phrase.role = stringOr(entry, "role");
            if (const auto it = entry.find("turnaround"); it != entry.end() && it->is_boolean()) {
                phrase.turnaround = it->get<bool>();
            }
            list.push_back(std::move(phrase));
        }
        if (usable) {
            draft.phrases = std::move(list);
        }
    }

    const auto voices = root.find("voices");
    if (voices == root.end() || !voices->is_array()) {
        return fail("voices: missing or not an array");
    }
    if (voices->size() > static_cast<size_t>(mm::core::kMaxVoices)) {
        return fail("voices: too many voices");
    }
    for (size_t i = 0; i < voices->size(); ++i) {
        const json& entry = (*voices)[i];
        const std::string path = "voices[" + std::to_string(i) + "]";
        if (!entry.is_object()) {
            ++result.droppedVoices;
            continue;
        }
        AiVoiceDraft voice;
        const auto role = entry.find("role");
        if (role == entry.end() || !role->is_string()) {
            ++result.droppedVoices;
            continue;
        }
        voice.role = role->get<std::string>();
        voice.archetype = stringOr(entry, "archetype");
        if (const auto it = entry.find("motif_bars"); it != entry.end()) {
            int bars = 0;
            if (readInt(*it, bars) && bars > 0) {
                voice.motifBars = static_cast<uint32_t>(bars);
            }
        }
        const auto notes = entry.find("notes");
        if (notes == entry.end() || !notes->is_array()) {
            return fail(path + ".notes: missing or not an array");
        }
        if (notes->size() > kMaxNotesPerVoice) {
            return fail(path + ".notes: too many notes");
        }
        for (const auto& note : *notes) {
            readNote(note, voice, result.droppedNotes);
        }
        draft.voices.push_back(std::move(voice));
    }
    if (draft.voices.empty()) {
        return fail("voices: no usable voice");
    }
    result.ok = true;
    return result;
}

namespace {

BuildResult refuse(std::string error) {
    BuildResult result;
    result.error = std::move(error);
    return result;
}

/// "A", "C#", "Bb" (upper-case letter, then one optional # or b).
std::optional<mm::core::PitchClass> parseRootName(std::string_view name) {
    if (name.empty() || name.size() > 2) {
        return std::nullopt;
    }
    int base = 0;
    switch (name[0]) {
    case 'C':
        base = 0;
        break;
    case 'D':
        base = 2;
        break;
    case 'E':
        base = 4;
        break;
    case 'F':
        base = 5;
        break;
    case 'G':
        base = 7;
        break;
    case 'A':
        base = 9;
        break;
    case 'B':
        base = 11;
        break;
    default:
        return std::nullopt;
    }
    if (name.size() == 2) {
        if (name[1] == '#') {
            ++base;
        } else if (name[1] == 'b') {
            --base;
        } else {
            return std::nullopt;
        }
    }
    return static_cast<mm::core::PitchClass>((base + 12) % 12);
}

std::optional<mm::core::PhraseRole> parsePhraseRole(std::string_view role) {
    using mm::core::PhraseRole;
    if (role == "main") {
        return PhraseRole::Main;
    }
    if (role == "variation") {
        return PhraseRole::Variation;
    }
    if (role == "build") {
        return PhraseRole::Build;
    }
    if (role == "breakdown") {
        return PhraseRole::Breakdown;
    }
    if (role == "answer") {
        return PhraseRole::Answer;
    }
    return std::nullopt;
}

/// One chord per bar, or two with "x|y"; the list repeats over the pattern. Empty with `error` set when it is no use.
std::vector<mm::core::ChordEvent> buildProgression(const std::vector<std::string>& entries, uint32_t bars,
                                                   std::string& error) {
    std::vector<mm::core::ChordEvent> events;
    if (entries.empty() || bars % entries.size() != 0) {
        error = "context.progression: the number of entries must divide the number of bars";
        return events;
    }
    for (uint32_t bar = 0; bar < bars; ++bar) {
        const std::string& entry = entries[bar % entries.size()];
        const auto bar2 = entry.find('|');
        const bool split = bar2 != std::string::npos;
        if (split && entry.find('|', bar2 + 1) != std::string::npos) {
            error = "context.progression[" + std::to_string(bar % entries.size()) + "]: at most two chords per bar";
            return {};
        }
        const std::string first = split ? entry.substr(0, bar2) : entry;
        const auto chord = mm::core::parseChordSymbol(first);
        const auto second = split ? mm::core::parseChordSymbol(entry.substr(bar2 + 1)) : chord;
        if (!chord || !second) {
            error = "context.progression[" + std::to_string(bar % entries.size()) + "]: unknown chord symbol";
            return {};
        }
        mm::core::ChordEvent event;
        event.chord = *chord;
        event.startHalfBar = bar * 2;
        event.lengthHalfBars = split ? 1 : 2;
        events.push_back(event);
        if (split) {
            event.chord = *second;
            event.startHalfBar = bar * 2 + 1;
            event.lengthHalfBars = 1;
            events.push_back(event);
        }
    }
    return events;
}

} // namespace

BuildResult buildPattern(const AiDraft& draft, const BuildContext& context) {
    using namespace mm::core;
    if (!isValidPatternLength(context.lengthBars)) {
        return refuse("pattern: invalid length");
    }
    BuildResult result;
    Pattern pattern = makeEmptyPattern(context.lengthBars, context.styleId);

    const auto root = parseRootName(draft.root);
    if (!root) {
        return refuse("context.root: unknown note name '" + draft.root + "'");
    }
    const Scale* scale = findScale(draft.scale);
    if (scale == nullptr) {
        return refuse("context.scale: unknown scale '" + draft.scale + "'");
    }
    pattern.context.root = *root;
    pattern.context.scaleId = std::string(scale->id);
    std::string error;
    pattern.context.progression = buildProgression(draft.progression, context.lengthBars, error);
    if (pattern.context.progression.empty()) {
        return refuse(error);
    }

    // Phrases: the answer's when they are a valid form plan, else the single main phrase of makeEmptyPattern.
    if (!draft.phrases.empty()) {
        std::vector<Phrase> phrases;
        bool usable = true;
        for (const auto& entry : draft.phrases) {
            const auto role = parsePhraseRole(entry.role);
            if (!role || entry.startBar < 0 || entry.bars < 1) {
                usable = false;
                break;
            }
            Phrase phrase;
            phrase.startBar = static_cast<uint32_t>(entry.startBar);
            phrase.lengthBars = static_cast<uint32_t>(entry.bars);
            phrase.role = *role;
            phrase.turnaround = entry.turnaround && entry.bars >= 8;
            phrases.push_back(phrase);
        }
        if (usable && isValidFormPlan(context.lengthBars, phrases)) {
            pattern.phrases = std::move(phrases);
        }
    }

    if (context.style != nullptr) {
        pattern.kickGridId = context.style->kickDefault;
        pattern.info.styleProfileVersion = context.style->version;
    }
    const RegisterProfile registers = context.style != nullptr ? context.style->registerProfile() : RegisterProfile{};

    const uint32_t totalSteps = context.lengthBars * kStepsPerBar;
    std::set<uint32_t> usedIds;
    uint32_t maxKept = 0;
    size_t notesBuilt = 0;
    for (const auto& voice : draft.voices) {
        if (voice.role != "bass" && voice.role != "melody") {
            ++result.ignoredVoices;
        }
    }
    for (size_t index = 0; index < pattern.voices.size(); ++index) {
        Track& track = pattern.voices[index];
        const std::string wanted = track.role == VoiceRole::Bass ? "bass" : "melody";
        const auto found = std::find_if(draft.voices.begin(), draft.voices.end(),
                                        [&](const AiVoiceDraft& voice) { return voice.role == wanted; });
        if (found == draft.voices.end()) {
            return refuse("voices: no " + wanted + " voice");
        }
        const AiVoiceDraft& voice = *found;
        if (const Archetype* archetype = findArchetype(voice.archetype);
            archetype != nullptr && archetype->role == track.role) {
            track.archetypeId = voice.archetype;
            track.archetypeAuto = false;
        }
        const auto range = effectiveRange(registers, track.role, track.archetypeId, pattern.voicing, 0);
        if (!range) {
            return refuse("voices: the range of the " + wanted + " voice is too narrow");
        }
        const auto base = voiceBase(*root, range->low, range->high);
        if (!base) {
            return refuse("voices: no voice base for the " + wanted + " voice");
        }
        for (const auto& draftNote : voice.notes) {
            if (draftNote.step < 0 || static_cast<uint32_t>(draftNote.step) >= totalSteps) {
                ++result.droppedNotes;
                continue;
            }
            const int alt = std::clamp(draftNote.alt, -1, 1);
            const int octave = std::clamp(draftNote.octave, -4, 4);
            const int maxLen = static_cast<int>(totalSteps) - draftNote.step;
            const int len = std::clamp(draftNote.len, 1, maxLen);
            const int vel = std::clamp(draftNote.vel, 1, 127);
            result.clampedValues +=
                (alt != draftNote.alt) + (octave != draftNote.octave) + (len != draftNote.len) + (vel != draftNote.vel);
            const auto pitch = degreeToMidi(*scale, *base, draftNote.degree, alt, octave);
            if (!pitch) {
                ++result.droppedNotes;
                continue;
            }
            Note note;
            note.pitch = static_cast<uint8_t>(*pitch);
            note.startTick = static_cast<uint32_t>(draftNote.step) * kTicksPerStep;
            note.lengthTicks = static_cast<uint32_t>(len) * kTicksPerStep;
            note.velocity = static_cast<uint8_t>(vel);
            note.accent = draftNote.accent;
            note.slide = draftNote.slide;
            // An id keeps its note only when it is known and used for the first time; others are new notes.
            if (draftNote.id && context.knownIds != nullptr && context.knownIds->count(*draftNote.id) != 0 &&
                usedIds.insert(*draftNote.id).second) {
                note.id = *draftNote.id;
                maxKept = std::max(maxKept, note.id);
            }
            track.notes.push_back(note);
            ++notesBuilt;
        }
    }
    if (notesBuilt == 0) {
        return refuse("voices: no usable note");
    }

    // New notes get ids above every kept one and above the counter of the pattern being refined.
    pattern.nextNoteId = std::max(context.firstNewId, maxKept + 1);
    for (auto& track : pattern.voices) {
        for (auto& note : track.notes) {
            if (note.id == 0) {
                note.id = allocateNoteId(pattern);
            }
        }
        std::stable_sort(track.notes.begin(), track.notes.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    }

    pattern.info.source = "ai";
    pattern.info.prompt = context.prompt;
    pattern.info.promptVersion = context.promptVersion;
    pattern.info.providerId = context.providerId;
    pattern.info.modelId = context.modelId;
    pattern.info.rawResponse = context.rawResponse;
    pattern.info.energyPct = static_cast<uint8_t>(std::clamp(context.energyPct, 0, 100));
    pattern.info.creativityPct = static_cast<uint8_t>(std::clamp(context.creativityPct, 0, 100));
    pattern.info.createdUnixMs = context.createdUnixMs;

    if (const auto issues = validatePattern(pattern); !issues.empty()) {
        return refuse("pattern: " + issues.front());
    }
    result.ok = true;
    result.pattern = std::move(pattern);
    return result;
}

} // namespace mm::ai
