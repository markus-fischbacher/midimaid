#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace mm::core {

/// Quality rating and candidate selection (SPEC 4.3, 4.4, STYLES.md 1.14). All values are whole numbers; the
/// definitions of the soft criteria are start values for the listening test of phase 1a (D-111).

/// The soft criteria, each 0-100 (a voice without notes does not count; with no voice at all a criterion is 100).
struct SoftScores {
    int motif = 100;         ///< repetition of the bars of a voice
    int range = 100;         ///< span of the voices
    int density = 100;       ///< expected notes per bar inside the target band of the energy
    int kick = 100;          ///< bass notes off the kick steps
    int intervals = 100;     ///< strong-step intervals against the bass (STYLES.md 1.8)
    int rhythmVariety = 100; ///< different note lengths

    bool operator==(const SoftScores&) const = default;
};

struct QualityContext {
    int energyPct = 50;
    int creativityPct = 40;
    int chromaticPercent = 0;      ///< from 30 on tense intervals are allowed (STYLES.md 1.8)
    bool harshStyle = false;       ///< Hard/Industrial: tense intervals are always allowed
    std::vector<bool> ignoresKick; ///< per voice (pattern order): the archetype may hold over kicks (`long_tied`)

    bool operator==(const QualityContext&) const = default;
};

/// Target band of expected notes per bar in hundredths (a note with chance < 100 counts less). The band moves with the
/// energy (0-100 %).
struct DensityBand {
    int low = 0;
    int high = 0;
};
DensityBand densityBand(VoiceRole role, int energyPct);

constexpr int kBassSpanLimit = 12;   ///< semitones, fully rated up to here
constexpr int kMelodySpanLimit = 19; ///< octave plus fifth
constexpr int kMelodyMinSpan = 3;    ///< a melody of 3 or more notes needs at least this span

SoftScores scoreCriteria(const Pattern& pattern, const QualityContext& context);

/// Weighted mean of the criteria with the weights of the profile, rounded half up to 0-100. With high creativity the
/// weight of the motif criterion shrinks to `(100 - creativity / 2) %` (SPEC 7.6). All weights 0 give 100.
int overallScore(const SoftScores& scores, const QualityProfile& weights, int creativityPct);

constexpr int kCandidatesPerRound = 8;
constexpr int kMaxRounds = 3; ///< the first round and up to two further rounds (D-88)

using CandidateGenerator = std::function<Pattern(uint64_t seed)>;
using HardCheck = std::function<bool(const Pattern&)>; ///< false rejects a candidate (copy protection, later)

struct SelectionResult {
    bool success =
        false;         ///< false: no valid candidate, the previous pattern stays (nothing below the minimum is taken)
    Pattern pattern;   ///< the winner, empty on failure
    uint64_t seed = 0; ///< the seed of the winner, replayed with `generate(seed)` without a selection
    int score = 0;
    SoftScores scores;
    int rounds = 0;     ///< rounds that were played
    int candidates = 0; ///< candidates that were generated and rated
};

/// The seed of candidate `index` of round `round` (0-based), derived from the initial seed.
uint64_t candidateSeed(uint64_t initialSeed, int round, int index);

/// Generates 8 candidates per round, keeps the valid ones (hard check passed, score at least `profile.minScore`) and
/// returns the best; a tie goes to the lower index. Without a valid candidate up to two more rounds follow.
SelectionResult selectBest(const CandidateGenerator& generate, uint64_t initialSeed, const QualityProfile& profile,
                           const QualityContext& context, const HardCheck& hardCheck = {});

/// Stores the quality score and the winner seed in the pattern (`qualityScore`, `info.winnerSeed`).
void applyResult(Pattern& pattern, const SelectionResult& result);

} // namespace mm::core
