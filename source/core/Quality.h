#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <optional>
#include <functional>
#include <vector>

namespace mm::core {

/// Quality rating and candidate selection (SPEC 4.3, 4.4, STYLES.md 1.14). All values are whole numbers; the
/// definitions of the soft criteria are start values for the listening test of phase 1a (D-111).

/// The soft criteria, each 0-100 (a voice without notes does not count; with no voice at all a criterion is 100).
struct SetProfile;
struct ReferenceEntry;

struct SoftScores {
    int motif = 100;         ///< repetition of the bars of a voice
    int range = 100;         ///< span of the voices
    int density = 100;       ///< expected notes per bar inside the target band of the energy
    int kick = 100;          ///< bass notes off the kick steps
    int intervals = 100;     ///< strong-step intervals against the bass (STYLES.md 1.8)
    int rhythmVariety = 100; ///< different note lengths
    int proximity = 100;     ///< closeness to the profile of the reference set (v1.1, D-242)
    /// How much the proximity counts, in percent of its weight in the profile: 2 x "Persönlich" with a profile that has
    /// lines for the voices of the pattern, else 0 (the criterion is not there).
    int proximityPct = 0;
    int historyDistance = 100; ///< distance to the last results of the slot: 100 minus the highest similarity (v1.1, D-244)
    /// 200 when the generation has a history to keep away from (counts with its full weight), else 0.
    int historyDistancePct = 0;

    bool operator==(const SoftScores&) const = default;
};

struct QualityContext {
    int energyPct = 50;
    int creativityPct = 40;
    int chromaticPercent = 0;      ///< from 30 on tense intervals are allowed (STYLES.md 1.8)
    bool harshStyle = false;       ///< Hard/Industrial: tense intervals are always allowed
    std::vector<bool> ignoresKick; ///< per voice (pattern order): the archetype may hold over kicks (`long_tied`)
    const SetProfile* profile = nullptr; ///< the profile of the reference set (non-owning, null: no proximity, D-242)
    int personalPct = 50;                ///< 0-100: the weight of the proximity
    /// The entries of the history to keep away from (non-owning, null or empty: no criterion, D-244).
    const std::vector<ReferenceEntry>* history = nullptr;

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

/// How close the voices of the pattern are to the profile of the reference set, 0-100 (D-242): for every bass and melody
/// voice whose role has lines in the profile, two thirds the rhythm (100 minus the mean distance, in tenths of a percent,
/// between the share of bars with a note start on a step and the profile's `stepPermille`) and one third the density
/// (the smaller of notes per bar and the profile's, over the larger). The mean over these voices. nullopt when the
/// profile is invalid or has no lines for any voice of the pattern.
std::optional<int> proximityScore(const Pattern& pattern, const SetProfile& profile);

/// How far the pattern is from the entries of `history`, 0-100 (D-244): 100 minus the highest similarity in percent that
/// `checkCopyProtection` finds (same metric as the repetition protection: distinctive lines of the same role only, locked
/// voices left out). 100 when nothing could be compared.
int historyDistanceScore(const Pattern& pattern, const std::vector<ReferenceEntry>& history);

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

/// Same, with a context per candidate (the archetypes of a candidate decide `ignoresKick`).
using ContextFor = std::function<QualityContext(const Pattern&)>;
SelectionResult selectBest(const CandidateGenerator& generate, uint64_t initialSeed, const QualityProfile& profile,
                           const ContextFor& contextFor, const HardCheck& hardCheck = {});

/// Stores the quality score and the winner seed in the pattern (`qualityScore`, `info.winnerSeed`).
void applyResult(Pattern& pattern, const SelectionResult& result);

} // namespace mm::core
