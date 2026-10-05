#include "core/Quality.h"

#include "core/Constraints.h"
#include "core/KickGrid.h"
#include "core/Random.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

namespace mm::core {

namespace {

/// Mean of the values rounded half up; 100 for no values (nothing to criticise).
int meanOrFull(const std::vector<int>& values) {
    if (values.empty()) {
        return 100;
    }
    int sum = 0;
    for (const int v : values) {
        sum += v;
    }
    const int count = static_cast<int>(values.size());
    return (sum + count / 2) / count;
}

int clampScore(int value) {
    return std::clamp(value, 0, 100);
}

using BarSignature = std::vector<std::tuple<uint32_t, uint32_t, int>>;

int motifScore(const Track& track, uint32_t lengthBars) {
    std::vector<BarSignature> bars(lengthBars);
    for (const Note& note : track.notes) {
        const uint32_t bar = note.startTick / kTicksPerBar;
        if (bar < lengthBars) {
            bars[bar].emplace_back(note.startTick % kTicksPerBar, note.lengthTicks, note.pitch);
        }
    }
    std::vector<BarSignature> filled;
    for (BarSignature& bar : bars) {
        if (!bar.empty()) {
            std::sort(bar.begin(), bar.end());
            filled.push_back(std::move(bar));
        }
    }
    if (filled.size() <= 1) {
        return 100; // nothing to repeat: the loop itself is the repetition
    }
    int duplicated = 0;
    for (size_t i = 0; i < filled.size(); ++i) {
        for (size_t j = 0; j < filled.size(); ++j) {
            if (i != j && filled[i] == filled[j]) {
                ++duplicated;
                break;
            }
        }
    }
    // A A A A' (75 % of the bars have a twin) is the full score
    return clampScore(duplicated * 400 / (3 * static_cast<int>(filled.size())));
}

int rangeScore(const Track& track) {
    int low = 128;
    int high = -1;
    for (const Note& note : track.notes) {
        low = std::min<int>(low, note.pitch);
        high = std::max<int>(high, note.pitch);
    }
    const int span = high - low;
    const bool bass = track.role == VoiceRole::Bass;
    if (!bass && track.notes.size() >= 3 && span < kMelodyMinSpan) {
        return 0;
    }
    const int limit = bass ? kBassSpanLimit : kMelodySpanLimit;
    return clampScore(100 - 5 * std::max(0, span - limit));
}

int densityScore(const Track& track, uint32_t lengthBars, int energyPct) {
    // expected onsets per bar in hundredths: one onset per start tick, counted with the highest chance of its notes
    std::map<uint32_t, int> chanceAt;
    for (const Note& note : track.notes) {
        int& chance = chanceAt[note.startTick];
        chance = std::max<int>(chance, std::min<int>(note.chance, 100));
    }
    int total = 0;
    for (const auto& entry : chanceAt) {
        total += entry.second;
    }
    const int perBar = total / static_cast<int>(std::max<uint32_t>(lengthBars, 1));
    const DensityBand band = densityBand(track.role, energyPct);
    const int deviation = perBar < band.low ? band.low - perBar : perBar > band.high ? perBar - band.high : 0;
    return clampScore(100 - deviation / 5); // 20 points per note of deviation
}

int varietyScore(const Track& track) {
    std::set<uint32_t> lengths;
    for (const Note& note : track.notes) {
        lengths.insert(note.lengthTicks);
    }
    return clampScore((static_cast<int>(lengths.size()) - 1) * 50);
}

} // namespace

DensityBand densityBand(VoiceRole role, int energyPct) {
    const int e = std::clamp(energyPct, 0, 100);
    if (role == VoiceRole::Bass) {
        return {400 + 8 * e, 800 + 8 * e};
    }
    return {100 + 3 * e, 300 + 5 * e};
}

SoftScores scoreCriteria(const Pattern& pattern, const QualityContext& context) {
    std::vector<int> motif;
    std::vector<int> range;
    std::vector<int> density;
    std::vector<int> variety;
    for (const Track& track : pattern.voices) {
        if (track.notes.empty()) {
            continue;
        }
        motif.push_back(motifScore(track, pattern.lengthBars));
        range.push_back(rangeScore(track));
        density.push_back(densityScore(track, pattern.lengthBars, context.energyPct));
        variety.push_back(varietyScore(track));
    }

    // kick: bass notes that start on a kick step
    std::vector<int> kick;
    const std::vector<uint32_t> kicks = kickTicks(pattern);
    for (size_t v = 0; v < pattern.voices.size(); ++v) {
        const Track& track = pattern.voices[v];
        const bool ignores = v < context.ignoresKick.size() && context.ignoresKick[v];
        if (track.role != VoiceRole::Bass || track.notes.empty() || ignores) {
            continue;
        }
        int hits = 0;
        for (const Note& note : track.notes) {
            hits += std::binary_search(kicks.begin(), kicks.end(), note.startTick) ? 1 : 0;
        }
        const int total = static_cast<int>(track.notes.size());
        kick.push_back((total - hits) * 100 / total);
    }

    // intervals: notes of the other voices that sound with the first bass voice on a strong step
    int relevant = 0;
    int clean = 0;
    const Track* bass = nullptr;
    for (const Track& track : pattern.voices) {
        if (track.role == VoiceRole::Bass) {
            bass = &track;
            break;
        }
    }
    if (bass != nullptr) {
        const bool tensionAllowed = context.chromaticPercent >= 30 || context.harshStyle;
        for (const Track& track : pattern.voices) {
            if (track.role == VoiceRole::Bass) {
                continue;
            }
            for (const Note& note : track.notes) {
                if (!soundsWithBass(*bass, note.startTick, note.lengthTicks)) {
                    continue;
                }
                ++relevant;
                clean += violatesBassRules(*bass, note.startTick, note.lengthTicks, note.pitch, tensionAllowed) ? 0 : 1;
            }
        }
    }

    SoftScores scores;
    scores.motif = meanOrFull(motif);
    scores.range = meanOrFull(range);
    scores.density = meanOrFull(density);
    scores.kick = meanOrFull(kick);
    scores.intervals = relevant == 0 ? 100 : clean * 100 / relevant;
    scores.rhythmVariety = meanOrFull(variety);
    return scores;
}

int overallScore(const SoftScores& scores, const QualityProfile& weights, int creativityPct) {
    const int creativity = std::clamp(creativityPct, 0, 100);
    // every weight is scaled by 200; the motif weight by (200 - creativity), which is (100 - creativity / 2) %
    const int64_t motifFactor = 200 - creativity;
    const int64_t pairs[][2] = {{std::max(weights.motif, 0) * motifFactor, scores.motif},
                                {std::max(weights.range, 0) * int64_t{200}, scores.range},
                                {std::max(weights.density, 0) * int64_t{200}, scores.density},
                                {std::max(weights.kick, 0) * int64_t{200}, scores.kick},
                                {std::max(weights.intervals, 0) * int64_t{200}, scores.intervals},
                                {std::max(weights.rhythmVariety, 0) * int64_t{200}, scores.rhythmVariety}};
    int64_t numerator = 0;
    int64_t denominator = 0;
    for (const auto& pair : pairs) {
        numerator += pair[0] * pair[1];
        denominator += pair[0];
    }
    if (denominator == 0) {
        return 100;
    }
    return static_cast<int>(std::clamp<int64_t>((2 * numerator + denominator) / (2 * denominator), 0, 100));
}

uint64_t candidateSeed(uint64_t initialSeed, int round, int index) {
    return deriveSeed(initialSeed, static_cast<uint64_t>(round * kCandidatesPerRound + index));
}

SelectionResult selectBest(const CandidateGenerator& generate, uint64_t initialSeed, const QualityProfile& profile,
                           const QualityContext& context, const HardCheck& hardCheck) {
    SelectionResult result;
    for (int round = 0; round < kMaxRounds; ++round) {
        ++result.rounds;
        bool found = false;
        for (int index = 0; index < kCandidatesPerRound; ++index) {
            const uint64_t seed = candidateSeed(initialSeed, round, index);
            Pattern candidate = generate(seed);
            ++result.candidates;
            if (hardCheck && !hardCheck(candidate)) {
                continue;
            }
            const SoftScores scores = scoreCriteria(candidate, context);
            const int score = overallScore(scores, profile, context.creativityPct);
            if (score < profile.minScore) {
                continue;
            }
            if (!found || score > result.score) { // a tie keeps the lower index
                found = true;
                result.pattern = std::move(candidate);
                result.seed = seed;
                result.score = score;
                result.scores = scores;
            }
        }
        if (found) {
            result.success = true;
            return result;
        }
    }
    return result;
}

void applyResult(Pattern& pattern, const SelectionResult& result) {
    pattern.qualityScore = static_cast<uint8_t>(std::clamp(result.score, 0, 100));
    pattern.info.winnerSeed = result.seed;
}

} // namespace mm::core
