#pragma once

#include "core/Analysis.h"
#include "core/CopyProtection.h"
#include "core/Progression.h"
#include "core/ReferenceSet.h"
#include "core/SetProfile.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mm::core {

/// What the reference set of a style does to a generation (SPEC 3.20, v1.0, D-182): the entries for the copy
/// protection, the progressions that join the progression generator and the examples for the AI. Pure functions of the
/// set; nothing here reads a file.

/// The id every pattern gets that was generated with a set (v1.0 has one set per style; v1.1 adds named sets).
inline const char* const kReferenceSetId = "default";

/// The progressions of the set only take effect from this many active items on (SPEC 3.20).
constexpr size_t kMinActiveForProgressions = 5;
constexpr size_t kMaxPersonalExamples = 3; ///< examples in one AI request
constexpr uint32_t kMaxExampleBars = 4;    ///< bars of one example
constexpr size_t kMaxExampleNotes = 64;    ///< notes of one example
/// A progression of an item is used when the analysis is at least this sure of it (0 to 1).
constexpr double kMinProgressionConfidence = 0.3;

/// The active bass and melody items as entries of the copy protection: the voices of a pattern are bass and melody, so
/// drum and chord tracks protect nothing. Empty set or no such item: empty.
std::vector<ReferenceEntry> protectionEntries(const ReferenceSet& set);

/// The chord cycles of the active items (not drums), weighted: weight of the item (1 to 5) times the confidence of its
/// progression in percent / 10, summed for equal cycles, sorted by chords. The cycle is the shortest repetition of 2 to
/// 4 chords in the sequence of the track (consecutive equal chords count once); a track without one, with a single
/// chord or with a progression below `kMinProgressionConfidence` gives none. Fewer than `kMinActiveForProgressions`
/// active items: empty.
std::vector<WeightedProgression> personalProgressions(const ReferenceSet& set);

/// One note of an example: the 16th step, the semitones above the key root (counted from the octave of the lowest
/// note, so a bass that stays low has small numbers) and the length in 16ths.
struct ExampleNote {
    uint32_t step = 0;
    int semitones = 0;
    uint32_t lengthSteps = 1;

    bool operator==(const ExampleNote&) const = default;
};

/// A piece of the musician's own material for the prompt (at most `kMaxExampleBars` bars).
struct PersonalExample {
    TrackRole role = TrackRole::Melody;
    PitchClass root = 0;
    std::string scaleId;
    uint32_t bars = 1;
    std::vector<ExampleNote> notes;

    bool operator==(const PersonalExample&) const = default;
};

/// Everything a generation takes from the set except the examples: `setId` is `kReferenceSetId` when at least one item
/// is active (an inactive set is no set), else empty.
struct ReferenceEffect {
    std::vector<ReferenceEntry> entries;
    std::vector<WeightedProgression> progressions;
    SetProfile profile; ///< valid from `kMinActiveForProgressions` active items on (v1.1)
    std::string setId;
};
ReferenceEffect referenceEffectOf(const ReferenceSet& set);

/// Up to `max` examples from the active bass and melody items: the best bass, then the best melody, then the best of
/// the rest. "Best" is the highest weight, then the density closest to the energy (the share of 16th steps with a
/// note, from 0.15 at energy 0 to 0.75 at energy 100), then the id. Each example is cut to its first 4 bars.
std::vector<PersonalExample> personalExamples(const ReferenceSet& set, int energyPct,
                                              size_t max = kMaxPersonalExamples);

} // namespace mm::core
