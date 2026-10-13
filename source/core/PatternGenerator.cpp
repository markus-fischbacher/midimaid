#include "core/PatternGenerator.h"

#include "core/Chance.h"
#include "core/FormPlan.h"
#include "core/Groove.h"
#include "core/LockRestore.h"
#include "core/Progression.h"
#include "core/Ratchet.h"
#include "core/Turnaround.h"

#include <algorithm>

namespace mm::core {

namespace {

int scoreOf(const Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings) {
    const QualityContext context = qualityContextFor(pattern, style, settings);
    return overallScore(scoreCriteria(pattern, context), style.quality, settings.creativityPct);
}

void setManualArchetype(Track& track, const std::optional<std::string>& id) {
    if (id.has_value()) {
        track.archetypeId = *id;
        track.archetypeAuto = false;
    }
}

bool fillVoice(Pattern& pattern, size_t index, const StyleProfile& style, const ArchetypeSettings& settings,
               Pcg32& rng) {
    const std::string id =
        resolveArchetype(pattern.voices[index], style, settings.energyPct, rng, settings.creativityPct);
    return generateVoice(pattern, index, id, style, settings, rng);
}

/// Bass voices first, so that every other voice knows them; the order inside a role is the order of the pattern.
std::vector<size_t> generationOrder(const Pattern& pattern) {
    std::vector<size_t> order;
    for (size_t i = 0; i < pattern.voices.size(); ++i) {
        order.push_back(i);
    }
    std::stable_partition(order.begin(), order.end(),
                          [&pattern](size_t i) { return pattern.voices[i].role == VoiceRole::Bass; });
    return order;
}

ArchetypeSettings settingsForRole(const ArchetypeSettings& base, PhraseRole role) {
    ArchetypeSettings settings = base;
    settings.energyPct = std::clamp(base.energyPct + phraseEnergyDelta(role), 0, 100);
    settings.creativityPct = std::clamp(base.creativityPct + phraseCreativityDelta(role), 0, 100);
    return settings;
}

using VoiceNotes = std::vector<std::vector<Note>>; ///< per voice, absolute ticks, without ids

/// Generates all voices of one phrase; nullopt if one of them cannot be generated.
/// `keep[i]` (optional): the voice stays as it is in `pattern`; its notes of the phrase are the context of the others
/// and it gets no new notes.
std::optional<VoiceNotes> generatePhraseNotes(const Pattern& pattern, const Phrase& phrase,
                                              const std::vector<std::string>& archetypes, const StyleProfile& style,
                                              const ArchetypeSettings& base, uint64_t phraseSeed,
                                              const std::vector<bool>& keep = {}) {
    Pattern sub = phraseSkeleton(pattern, phrase);
    const auto kept = [&keep](size_t i) { return i < keep.size() && keep[i]; };
    const uint32_t phraseFrom = phrase.startBar * kTicksPerBar;
    const uint32_t phraseTo = phraseFrom + phrase.lengthBars * kTicksPerBar;
    for (size_t i = 0; i < sub.voices.size(); ++i) {
        if (!kept(i)) {
            continue;
        }
        for (Note note : pattern.voices[i].notes) {
            if (note.startTick >= phraseFrom && note.startTick < phraseTo) {
                note.startTick -= phraseFrom;
                sub.voices[i].notes.push_back(note);
            }
        }
    }
    const ArchetypeSettings settings = settingsForRole(base, phrase.role);
    Pcg32 rng = Pcg32::fromSeed(phraseSeed);
    for (const size_t index : generationOrder(sub)) {
        if (!kept(index) && !generateVoice(sub, index, archetypes[index], style, settings, rng)) {
            return std::nullopt;
        }
    }
    if (phrase.turnaround) {
        Phrase local = phrase;
        local.startBar = 0;
        std::vector<bool> varies(sub.voices.size());
        for (size_t i = 0; i < varies.size(); ++i) {
            varies[i] = !kept(i);
        }
        applyTurnaround(sub, local, settings.energyPct, phraseSeed, varies);
    }
    VoiceNotes result;
    for (size_t i = 0; i < sub.voices.size(); ++i) {
        std::vector<Note> notes = kept(i) ? std::vector<Note>{} : sub.voices[i].notes;
        for (Note& note : notes) {
            note.startTick += phrase.startBar * kTicksPerBar;
        }
        result.push_back(std::move(notes));
    }
    return result;
}

/// Phrases of one role share a seed, so that they repeat their rhythm (A ... A).
uint64_t phraseSeed(uint64_t seed, PhraseRole role) {
    return deriveSeed(seed, static_cast<uint64_t>(role) + 1);
}

void appendNotes(Pattern& pattern, size_t voice, std::vector<Note> notes) {
    for (Note& note : notes) {
        note.id = allocateNoteId(pattern);
        pattern.voices[voice].notes.push_back(note);
    }
}

/// Draws the ratchets and the chances of the voices (not `keep`) for the bars [fromBar, toBar), after the archetypes
/// are known.
void addRatchets(Pattern& pattern, const StyleProfile& style, int energyPct, uint64_t seed, uint32_t fromBar,
                 uint32_t toBar, const std::vector<bool>& keep = {}) {
    for (size_t i = 0; i < pattern.voices.size(); ++i) {
        if (i < keep.size() && keep[i]) {
            continue;
        }
        applyRatchets(pattern, i, style.id, energyPct, seed, fromBar, toBar);
        applyChances(pattern, i, style, energyPct, seed, fromBar, toBar);
    }
}

/// The reference profile a generation works with and the "Persönlich" share that goes with it (SPEC 3.20): the one of the
/// request, else the snapshot of `pattern` when a variation works on it, else none.
struct Resolved {
    std::optional<SetProfile> snapshot; ///< owns the profile read from a pattern
    const SetProfile* profile = nullptr;
    int personalPct = 50;
};

Resolved resolve(const GenerationRequest& request, const Pattern* pattern) {
    Resolved resolved;
    resolved.personalPct = request.settings.personalPct;
    if (request.profile && request.profile->valid) {
        resolved.profile = &*request.profile;
    } else if (pattern != nullptr && !pattern->info.profile.empty()) {
        resolved.snapshot = setProfileFromJson(pattern->info.profile);
        if (resolved.snapshot) {
            resolved.profile = &*resolved.snapshot;
            resolved.personalPct = pattern->info.personalPct;
        }
    }
    return resolved;
}

/// The settings the generators get: those of the request with the profile (non-owning, `resolved` must outlive them).
ArchetypeSettings settingsOf(const GenerationRequest& request, const Resolved& resolved) {
    ArchetypeSettings settings = request.settings;
    settings.profile = resolved.profile;
    settings.personalPct = resolved.personalPct;
    return settings;
}

/// The style a generation works with (SPEC 3.20, 3.8): the base style moved towards the profile, then with the chromatic
/// share of the request, else that of `pattern`, else the style's.
StyleProfile styleFor(const StyleProfile& base, const GenerationRequest& request, const Pattern* pattern,
                      const Resolved& resolved) {
    const StyleProfile moved =
        resolved.profile != nullptr ? personalizeStyle(base, *resolved.profile, resolved.personalPct) : base;
    const int chromatic =
        request.settings.chromaticPct >= 0 ? request.settings.chromaticPct : (pattern != nullptr ? pattern->info.chromaticPct : -1);
    return withChromatic(moved, chromatic);
}

/// One candidate around the locked voices of `current` (a pure function of `seed`, like `generateCandidate`): harmony,
/// length, form plan, kick grid and locked voices stay; the other voices are generated again. A candidate that cannot
/// be generated is an empty pattern without archetypes, which the hard check rejects.
Pattern generateAroundLocks(const StyleProfile& style, const GenerationRequest& request, const Pattern& current,
                            uint64_t seed) {
    if (request.cancel != nullptr && request.cancel->load(std::memory_order_relaxed)) {
        return makeEmptyPattern(current.lengthBars, style.id);
    }
    const Resolved resolved = resolve(request, nullptr);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    Pcg32 rng = Pcg32::fromSeed(seed);
    Pattern pattern = current;
    std::vector<bool> keep;
    for (Track& track : pattern.voices) {
        keep.push_back(isVoiceLocked(track));
        if (!keep.back()) {
            track.notes.clear();
        }
    }
    // a voice with locks in some dimensions is generated again and takes its locked dimensions back (D-231)
    const auto restorePartialLocks = [&] {
        for (size_t i = 0; i < pattern.voices.size() && i < current.voices.size(); ++i) {
            if (!keep[i] && hasAnyLock(current.voices[i])) {
                pattern.voices[i].notes =
                    restoreLocks(current.voices[i].notes, pattern.voices[i].notes, pattern.voices[i].lock);
            }
        }
    };
    const auto failed = [&] { return makeEmptyPattern(current.lengthBars, style.id); };
    if (pattern.phrases.size() <= 1) {
        for (const size_t index : generationOrder(pattern)) {
            if (!keep[index] && !fillVoice(pattern, index, style, settings, rng)) {
                return failed();
            }
        }
        if (!pattern.phrases.empty()) {
            std::vector<bool> varies(keep.size());
            for (size_t i = 0; i < keep.size(); ++i) {
                varies[i] = !keep[i];
            }
            applyTurnaround(pattern, pattern.phrases[0], settings.energyPct,
                            phraseSeed(seed, pattern.phrases[0].role), varies);
        }
    } else {
        std::vector<std::string> archetypes(pattern.voices.size());
        for (const size_t index : generationOrder(pattern)) {
            archetypes[index] = keep[index] ? pattern.voices[index].archetypeId
                                            : resolveArchetype(pattern.voices[index], style, settings.energyPct,
                                                               rng, settings.creativityPct);
        }
        for (const Phrase& phrase : pattern.phrases) {
            const auto notes = generatePhraseNotes(pattern, phrase, archetypes, style, settings,
                                                   phraseSeed(seed, phrase.role), keep);
            if (!notes.has_value()) {
                return failed();
            }
            for (size_t i = 0; i < pattern.voices.size(); ++i) {
                if (!keep[i]) {
                    appendNotes(pattern, i, (*notes)[i]);
                }
            }
        }
        for (size_t i = 0; i < pattern.voices.size(); ++i) {
            if (!keep[i]) {
                pattern.voices[i].archetypeId = archetypes[i];
            }
        }
    }
    restorePartialLocks();
    addRatchets(pattern, style, settings.energyPct, seed, 0, pattern.lengthBars, keep);
    applyConstraints(pattern, constraintSettingsFor(pattern, style));
    pattern.info.source = "algorithm";
    pattern.info.seed = request.seed;
    pattern.info.winnerSeed = seed;
    pattern.info.styleProfileVersion = style.version;
    pattern.info.creativityPct = static_cast<uint8_t>(std::clamp(settings.creativityPct, 0, 100));
    pattern.info.energyPct = static_cast<uint8_t>(std::clamp(settings.energyPct, 0, 100));
    pattern.info.chromaticPct = settings.chromaticPct < 0
                                    ? int16_t{-1}
                                    : static_cast<int16_t>(std::min(settings.chromaticPct, 100));
    pattern.info.referenceSetId = request.referenceSetId;
    if (request.profile && request.profile->valid) { // the snapshot of what the style was moved towards (SPEC 3.20)
        pattern.info.profile = setProfileToJson(*request.profile);
        pattern.info.personalPct = static_cast<uint8_t>(std::clamp(settings.personalPct, 0, 100));
    }
    return pattern;
}

} // namespace

Pattern generateCandidate(const StyleProfile& style, const GenerationRequest& request, uint64_t seed) {
    if (request.cancel != nullptr && request.cancel->load(std::memory_order_relaxed)) {
        return makeEmptyPattern(request.lengthBars, style.id); // no archetype: never a valid candidate
    }
    const Resolved resolved = resolve(request, nullptr);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    Pcg32 rng = Pcg32::fromSeed(seed);
    Pattern pattern = makeEmptyPattern(request.lengthBars, style.id);
    pattern.voicing.chordMemory = style.melody.chordMemory; // the style decides (STYLES.md 1.10, D-229)
    pattern.kickGridId = request.kickGridId.value_or(style.kickDefault);
    if (!request.voices.empty()) {
        pattern.voices.clear();
        for (size_t i = 0; i < request.voices.size() && i < static_cast<size_t>(kMaxVoices); ++i) {
            Track track;
            track.role = request.voices[i];
            track.midiChannel = static_cast<uint8_t>(i + 1);
            pattern.voices.push_back(std::move(track));
        }
    }
    applyHarmony(pattern, style, rng, request.root, request.scaleId,
                 scaledProgressions(!request.personalProgressions.empty() || !request.profile || !request.profile->valid
                                        ? request.personalProgressions
                                        : request.profile->progressions,
                                    settings.personalPct));
    applyStyleGroove(pattern, style);
    for (Track& track : pattern.voices) {
        setManualArchetype(track, track.role == VoiceRole::Bass ? request.bassArchetype : request.melodyArchetype);
    }
    std::vector<Phrase> phrases = generateFormPlan(style, request.lengthBars, rng);
    if (request.formPlan.has_value() && isValidFormPlan(request.lengthBars, *request.formPlan)) {
        phrases = *request.formPlan;
    }
    if (!request.formPlan.has_value() || !isValidFormPlan(request.lengthBars, *request.formPlan)) {
        assignTurnarounds(phrases, settings.energyPct, seed);
    }
    pattern.phrases = phrases;
    if (phrases.size() == 1) {
        for (const size_t index : generationOrder(pattern)) {
            fillVoice(pattern, index, style, settings, rng);
        }
        applyTurnaround(pattern, phrases[0], settings.energyPct, phraseSeed(seed, phrases[0].role));
    } else {
        std::vector<std::string> archetypes(pattern.voices.size());
        for (const size_t index : generationOrder(pattern)) {
            archetypes[index] = resolveArchetype(pattern.voices[index], style, settings.energyPct, rng,
                                                 settings.creativityPct);
        }
        std::vector<bool> complete(pattern.voices.size(), true);
        for (const Phrase& phrase : phrases) {
            const auto notes = generatePhraseNotes(pattern, phrase, archetypes, style, settings,
                                                   phraseSeed(seed, phrase.role));
            for (size_t i = 0; i < pattern.voices.size(); ++i) {
                if (notes.has_value()) {
                    appendNotes(pattern, i, (*notes)[i]);
                } else {
                    complete[i] = false;
                }
            }
        }
        for (size_t i = 0; i < pattern.voices.size(); ++i) {
            if (complete[i]) {
                pattern.voices[i].archetypeId = archetypes[i];
            }
        }
    }
    addRatchets(pattern, style, settings.energyPct, seed, 0, pattern.lengthBars);
    applyConstraints(pattern, constraintSettingsFor(pattern, style));

    pattern.info.source = "algorithm";
    pattern.info.seed = request.seed;
    pattern.info.winnerSeed = seed;
    pattern.info.styleProfileVersion = style.version;
    pattern.info.creativityPct = static_cast<uint8_t>(std::clamp(settings.creativityPct, 0, 100));
    pattern.info.energyPct = static_cast<uint8_t>(std::clamp(settings.energyPct, 0, 100));
    pattern.info.chromaticPct = settings.chromaticPct < 0
                                    ? int16_t{-1}
                                    : static_cast<int16_t>(std::min(settings.chromaticPct, 100));
    pattern.info.referenceSetId = request.referenceSetId;
    if (request.profile && request.profile->valid) { // the snapshot of what the style was moved towards (SPEC 3.20)
        pattern.info.profile = setProfileToJson(*request.profile);
        pattern.info.personalPct = static_cast<uint8_t>(std::clamp(settings.personalPct, 0, 100));
    }
    return pattern;
}

/// The selection with the copy protection against the reference set and the repetition protection against the history
/// (D-225). `voiceComplete` says whether a voice of a candidate counts as generated. When the history leaves no valid
/// candidate the selection runs again without it, so the repetition protection never blocks a generation.
static SelectionResult selectProtected(const GenerationRequest& request, const StyleProfile& style,
                                const CandidateGenerator& generate, const ContextFor& contextFor,
                                const std::function<bool(const Track&)>& voiceComplete) {
    const auto select = [&](const std::vector<ReferenceEntry>& history) {
        HardCheck copyCheck;
        if (!request.referenceSet.empty()) {
            copyCheck = copyProtectionCheck(request.referenceSet);
        }
        HardCheck repeatCheck;
        if (!history.empty()) {
            repeatCheck = copyProtectionCheck(history);
        }
        const HardCheck hardCheck = [copyCheck, repeatCheck, &voiceComplete](const Pattern& pattern) {
            const bool complete = std::all_of(pattern.voices.begin(), pattern.voices.end(), voiceComplete);
            return complete && (!copyCheck || copyCheck(pattern)) && (!repeatCheck || repeatCheck(pattern));
        };
        return selectBest(generate, request.seed, style.quality, contextFor, hardCheck);
    };
    SelectionResult result = select(request.history);
    if (!result.success && !request.history.empty() && !(request.cancel != nullptr && request.cancel->load())) {
        result = select({});
    }
    if (result.success) {
        applyResult(result.pattern, result);
    }
    return result;
}

SelectionResult generatePatternAroundLocks(const StyleProfile& baseStyle, const GenerationRequest& request,
                                           const Pattern& current) {
    const Resolved resolved = resolve(request, nullptr);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    const StyleProfile style = styleFor(baseStyle, request, nullptr, resolved);
    const bool anythingToGenerate = std::any_of(current.voices.begin(), current.voices.end(),
                                                [](const Track& track) { return !isVoiceLocked(track); });
    if (!anythingToGenerate) {
        return {};
    }
    const auto generate = [&](uint64_t seed) { return generateAroundLocks(style, request, current, seed); };
    const auto contextFor = [&](const Pattern& pattern) { return qualityContextFor(pattern, style, settings); };
    // the voices that are generated have an archetype; a locked one may have none (an imported or drawn line); the copy
    // and repetition protection leave locked voices alone
    return selectProtected(
        request, style, generate, contextFor, [](const Track& track) { return isVoiceLocked(track) || !track.archetypeId.empty(); });
}

SelectionResult generatePattern(const StyleProfile& baseStyle, const GenerationRequest& request) {
    const Resolved resolved = resolve(request, nullptr);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    const StyleProfile style = styleFor(baseStyle, request, nullptr, resolved);
    const auto generate = [&](uint64_t seed) { return generateCandidate(style, request, seed); };
    const auto contextFor = [&](const Pattern& pattern) { return qualityContextFor(pattern, style, settings); };
    // a candidate with a voice that could not be generated (it has no archetype) is never valid
    return selectProtected(request, style, generate, contextFor,
                           [](const Track& track) { return !track.archetypeId.empty(); });
}

Pattern replayWinner(const StyleProfile& baseStyle, const GenerationRequest& request, uint64_t winnerSeed) {
    const Resolved resolved = resolve(request, nullptr);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    const StyleProfile style = styleFor(baseStyle, request, nullptr, resolved);
    Pattern pattern = generateCandidate(style, request, winnerSeed);
    pattern.qualityScore = static_cast<uint8_t>(std::clamp(scoreOf(pattern, style, settings), 0, 100));
    return pattern;
}

bool regenerateVoice(Pattern& pattern, size_t voiceIndex, const StyleProfile& baseStyle, const GenerationRequest& request,
                     uint64_t seed) {
    // the share chosen in the request, else the one the pattern was made with, else the style's (SPEC 3.8)
    const Resolved resolved = resolve(request, &pattern);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    const StyleProfile style = styleFor(baseStyle, request, &pattern, resolved);
    if (voiceIndex >= pattern.voices.size()) {
        return false;
    }
    const LockFlags& lock = pattern.voices[voiceIndex].lock;
    if (lock.pitch && lock.rhythm && lock.velocity) {
        return false;
    }
    Pattern next = pattern;
    Pcg32 rng = Pcg32::fromSeed(seed);
    if (!fillVoice(next, voiceIndex, style, settings, rng)) {
        return false;
    }
    if (hasAnyLock(pattern.voices[voiceIndex])) { // the locked dimensions come back (D-231)
        next.voices[voiceIndex].notes = restoreLocks(pattern.voices[voiceIndex].notes, next.voices[voiceIndex].notes,
                                                     next.voices[voiceIndex].lock);
    }
    applyRatchets(next, voiceIndex, style.id, settings.energyPct, seed, 0, next.lengthBars);
    applyChances(next, voiceIndex, style, settings.energyPct, seed, 0, next.lengthBars);
    applyConstraints(next, constraintSettingsFor(next, style));
    next.qualityScore = static_cast<uint8_t>(std::clamp(scoreOf(next, style, settings), 0, 100));
    pattern = std::move(next);
    return true;
}

bool regeneratePhrase(Pattern& pattern, size_t phraseIndex, const StyleProfile& baseStyle, const GenerationRequest& request,
                      uint64_t seed) {
    const Resolved resolved = resolve(request, &pattern);
    const ArchetypeSettings settings = settingsOf(request, resolved);
    const StyleProfile style = styleFor(baseStyle, request, &pattern, resolved);
    if (phraseIndex >= pattern.phrases.size() || pattern.phrases[phraseIndex].locked) {
        return false;
    }
    std::vector<std::string> archetypes;
    for (const Track& track : pattern.voices) {
        if (track.archetypeId.empty()) {
            return false;
        }
        archetypes.push_back(track.archetypeId);
    }
    const Phrase phrase = pattern.phrases[phraseIndex];
    const auto notes = generatePhraseNotes(pattern, phrase, archetypes, style, settings, seed);
    if (!notes.has_value()) {
        return false;
    }
    Pattern next = pattern;
    const uint32_t from = phrase.startBar * kTicksPerBar;
    const uint32_t to = from + phrase.lengthBars * kTicksPerBar;
    for (size_t i = 0; i < next.voices.size(); ++i) {
        const LockFlags& lock = next.voices[i].lock;
        if (lock.pitch && lock.rhythm && lock.velocity) {
            continue;
        }
        auto& all = next.voices[i].notes;
        std::vector<Note> fresh = (*notes)[i];
        if (hasAnyLock(pattern.voices[i])) { // the locked dimensions of the phrase come back (D-231)
            std::vector<Note> before;
            for (const Note& note : all) {
                if (note.startTick >= from && note.startTick < to) {
                    before.push_back(note);
                }
            }
            fresh = restoreLocks(before, fresh, next.voices[i].lock);
        }
        all.erase(std::remove_if(all.begin(), all.end(),
                                 [&](const Note& note) { return note.startTick >= from && note.startTick < to; }),
                  all.end());
        appendNotes(next, i, fresh);
        std::stable_sort(all.begin(), all.end(),
                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
    }
    for (size_t i = 0; i < next.voices.size(); ++i) {
        const LockFlags& lock = next.voices[i].lock;
        if (!(lock.pitch && lock.rhythm && lock.velocity)) {
            applyRatchets(next, i, style.id, settings.energyPct, seed, phrase.startBar,
                          phrase.startBar + phrase.lengthBars);
            applyChances(next, i, style, settings.energyPct, seed, phrase.startBar,
                         phrase.startBar + phrase.lengthBars);
        }
    }
    applyConstraints(next, constraintSettingsFor(next, style));
    next.qualityScore = static_cast<uint8_t>(std::clamp(scoreOf(next, style, settings), 0, 100));
    pattern = std::move(next);
    return true;
}

} // namespace mm::core
