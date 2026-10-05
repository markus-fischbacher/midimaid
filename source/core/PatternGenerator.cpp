#include "core/PatternGenerator.h"

#include "core/Groove.h"
#include "core/Progression.h"

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

} // namespace

Pattern generateCandidate(const StyleProfile& style, const GenerationRequest& request, uint64_t seed) {
    Pcg32 rng = Pcg32::fromSeed(seed);
    Pattern pattern = makeEmptyPattern(request.lengthBars, style.id);
    pattern.kickGridId = request.kickGridId.value_or(style.kickDefault);
    applyHarmony(pattern, style, rng, request.root, request.scaleId);
    applyStyleGroove(pattern, style);
    for (Track& track : pattern.voices) {
        setManualArchetype(track, track.role == VoiceRole::Bass ? request.bassArchetype : request.melodyArchetype);
    }
    for (const size_t index : generationOrder(pattern)) {
        fillVoice(pattern, index, style, request.settings, rng);
    }
    applyConstraints(pattern, constraintSettingsFor(pattern, style));

    pattern.info.source = "algorithm";
    pattern.info.seed = request.seed;
    pattern.info.winnerSeed = seed;
    pattern.info.styleProfileVersion = style.version;
    pattern.info.creativityPct = static_cast<uint8_t>(std::clamp(request.settings.creativityPct, 0, 100));
    pattern.info.energyPct = static_cast<uint8_t>(std::clamp(request.settings.energyPct, 0, 100));
    return pattern;
}

SelectionResult generatePattern(const StyleProfile& style, const GenerationRequest& request) {
    const auto generate = [&](uint64_t seed) { return generateCandidate(style, request, seed); };
    const auto contextFor = [&](const Pattern& pattern) {
        return qualityContextFor(pattern, style, request.settings);
    };
    HardCheck copyCheck;
    if (!request.referenceSet.empty()) {
        copyCheck = copyProtectionCheck(request.referenceSet);
    }
    // a candidate with a voice that could not be generated (it has no archetype) is never valid
    const HardCheck hardCheck = [copyCheck](const Pattern& pattern) {
        const bool complete = std::all_of(pattern.voices.begin(), pattern.voices.end(),
                                          [](const Track& track) { return !track.archetypeId.empty(); });
        return complete && (!copyCheck || copyCheck(pattern));
    };
    SelectionResult result = selectBest(generate, request.seed, style.quality, contextFor, hardCheck);
    if (result.success) {
        applyResult(result.pattern, result);
    }
    return result;
}

Pattern replayWinner(const StyleProfile& style, const GenerationRequest& request, uint64_t winnerSeed) {
    Pattern pattern = generateCandidate(style, request, winnerSeed);
    pattern.qualityScore = static_cast<uint8_t>(std::clamp(scoreOf(pattern, style, request.settings), 0, 100));
    return pattern;
}

bool regenerateVoice(Pattern& pattern, size_t voiceIndex, const StyleProfile& style, const GenerationRequest& request,
                     uint64_t seed) {
    if (voiceIndex >= pattern.voices.size()) {
        return false;
    }
    const LockFlags& lock = pattern.voices[voiceIndex].lock;
    if (lock.pitch && lock.rhythm && lock.velocity) {
        return false;
    }
    Pattern next = pattern;
    Pcg32 rng = Pcg32::fromSeed(seed);
    if (!fillVoice(next, voiceIndex, style, request.settings, rng)) {
        return false;
    }
    applyConstraints(next, constraintSettingsFor(next, style));
    next.qualityScore = static_cast<uint8_t>(std::clamp(scoreOf(next, style, request.settings), 0, 100));
    pattern = std::move(next);
    return true;
}

} // namespace mm::core
