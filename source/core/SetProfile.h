#pragma once

#include "core/Analysis.h"
#include "core/Progression.h"
#include "core/ReferenceSet.h"
#include "core/StyleProfile.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// The profile of a reference set (SPEC 3.20, v1.1, D-240): what the active items of the set have in common, as numbers
/// the generators can use. A pure function of the set (`profileOf`), never stored with it: a change of the set is in
/// the next profile at once. Pure standard library; all arithmetic on integers, so the same set gives the same profile
/// on every platform.

constexpr size_t kMaxMotifCells = 8;     ///< per role
constexpr int kMinCellItems = 2;         ///< a cell is typical when this many items of the set hold it
constexpr size_t kMinCellNotes = 3;
constexpr size_t kMaxCellNotes = 16;

/// A typical piece of one or two bars of a line: the 16th steps of its notes and the interval of every note to the one
/// before (in semitones, the first one 0), so a cell does not depend on the key. `count` is the sum of the weights of
/// the items that hold it.
struct MotifCell {
    TrackRole role = TrackRole::Melody; ///< Bass or Melody
    uint8_t bars = 1;                   ///< 1 or 2
    std::vector<uint8_t> steps;         ///< 16th step of every note from the start of the cell (0 to 16 * bars - 1)
    std::vector<int8_t> intervals;      ///< same length as `steps`; the first is 0, the others -24 to 24
    int count = 0;

    bool operator==(const MotifCell&) const = default;
};

/// What the active lines of one role (bass or melody) have in common.
struct RoleStats {
    int items = 0;
    int notesPerBar100 = 0;                   ///< notes per bar in hundredths
    std::array<int, 16> stepPermille{};       ///< share of bars that have a note start on the step (0 to 1000)
    int spanSemitones = 0;                    ///< mean distance from the lowest to the highest tone
    int offbeatOffsetTicks = 0;               ///< mean timing offset of the notes on the steps 2, 6, 10 and 14 (the groove)
    int velocity = 0;                         ///< mean velocity
    std::array<int, 3> classPermille{};       ///< shares of calm, neutral and dense lines (the order of `ArchetypeDensity`)

    bool operator==(const RoleStats&) const = default;
};

struct SetProfile {
    bool valid = false;   ///< false: the set has too few active items (`kMinActiveForProgressions`); nothing else is set
    int activeItems = 0;
    std::vector<WeightedId> scales; ///< scale id and the weight of the items that are in it (sorted by id)
    int chromaticPermille = 0;      ///< share of the notes of lines outside the scale of their key
    RoleStats bass, melody;
    std::vector<MotifCell> cells;   ///< bass cells first, each role sorted by count (highest first), then by content
    std::vector<WeightedProgression> progressions;

    bool operator==(const SetProfile&) const = default;
};

/// Notes per bar from which a line counts as dense, and up to which as calm (bass; the melody has its own limits).
constexpr int kBassDenseNotesPerBar = 12;
constexpr int kBassCalmNotesPerBar = 5;
constexpr int kMelodyDenseNotesPerBar = 10;
constexpr int kMelodyCalmNotesPerBar = 4;

/// The profile of the active items of the set. Items without a bass or melody role count for the scales and the
/// progressions only; drums for nothing. An invalid profile when fewer than `kMinActiveForProgressions` items are
/// active, so a set takes effect only when it is large enough.
SetProfile profileOf(const ReferenceSet& set);

/// The cells of one role that two or more items hold: see `MotifCell`. Part of `profileOf`; separate for the tests.
std::vector<MotifCell> motifCellsOf(const ReferenceSet& set, TrackRole role);

/// One line for the view: the most frequent progression, the density of the bass and the melody, the preferred scale
/// and the number of cells, as a list of key and value pairs for the translation (empty for an invalid profile).
struct ProfileFacts {
    std::string progression; ///< chord symbols joined by "-", empty if none
    int bassNotesPerBar100 = 0;
    int melodyNotesPerBar100 = 0;
    std::string scaleId; ///< the heaviest scale, empty if none
    int scalePercent = 0;
    size_t cells = 0;
};
ProfileFacts factsOf(const SetProfile& profile);

/// ---- the snapshot (SPEC 3.20, 4.3) ----

/// The profile as compact JSON text: what a pattern keeps of the set it was made with, so that a variation or a replay
/// uses the same profile after the set has changed. Only a valid profile is written (an invalid one gives "").
std::string setProfileToJson(const SetProfile& profile);
/// nullopt for text that is not such a document (bad syntax, wrong types, values out of range, unknown scales or
/// chords, a cell that does not fit its bars); never throws. A valid result has `valid == true`.
std::optional<SetProfile> setProfileFromJson(std::string_view text);

/// ---- the effect on the style (SPEC 3.20, D-241) ----

/// The largest change the set makes to a weight of the style profile, in permille of the weight: 50 %.
constexpr int kMaxPersonalDeviationPermille = 500;
/// `personalizeStyle` returns the weights it changes in this multiple of the style's (all of one list alike).
constexpr int kWeightResolution = 100;
/// The chromatic default of a style moves by at most half of itself, but at least this many percentage points can
/// be reached (a style without chromatics would never move otherwise).
constexpr int kMinChromaticDeviationPercent = 5;

/// The style profile moved towards the set (`personalPct` 0 to 100; 0 or an invalid profile changes nothing):
/// - **scales:** the weight of every scale of the style times the ratio of its share in the set to its share in the
///   style, cut to 50 % to 150 %, then moved by `personalPct` percent of the way (so 50 gives at most 25 %);
/// - **archetypes:** the same for the weights of the bass and melody archetypes, per density class (calm, neutral, dense),
///   with the shares of the classes of the lines of the set (a role with no line in the set stays as it is);
/// - **chromatic default:** the share of the set, moved by `personalPct` percent of the way, by at most half of the
///   style's value (at least `kMinChromaticDeviationPercent` points).
/// No weight falls to 0 (a scale or archetype that the style has stays possible). Pure integer arithmetic.
StyleProfile personalizeStyle(const StyleProfile& style, const SetProfile& profile, int personalPct);

/// The personal progressions with their weights scaled by `personalPct` / 50 (50 leaves them as they are, 100 doubles
/// them, 0 drops them); the generator still cuts their sum to that of the style's own progressions.
std::vector<WeightedProgression> scaledProgressions(const std::vector<WeightedProgression>& progressions,
                                                    int personalPct);

} // namespace mm::core
