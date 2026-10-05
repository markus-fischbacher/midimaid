#pragma once

#include "core/Pattern.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm::core {

/// Effective pitch range of one voice as MIDI numbers (SPEC 4.2). The effective range already includes the octave
/// offset of the voice; the profiles and the octave offset arrive with their own roadmap items.
struct VoiceConstraints {
    int rangeLow = 28;
    int rangeHigh = 52;
};

struct ConstraintSettings {
    /// One entry per voice of the pattern, same order. Missing entries fall back to the role defaults.
    std::vector<VoiceConstraints> voices;
    uint32_t gridTicks = 240;    ///< note starts snap to this grid; 0 = off
    bool acceptTriplets = false; ///< starts on the 16th-triplet grid (160 ticks) are kept
    uint32_t minNoteTicks = 60;  ///< shorter notes are dropped (1/64); the SPEC minimum of one grid step would
                                 ///< forbid the 50-75 % notes of the rolling bass archetypes (D-101)
    int chromaticPercent = 0;    ///< share of notes that may leave the scale (SPEC 3.8)

    /// Defaults per voice role (STYLES.md 1.6: bass 28-52, melody 55-88) for every voice of the pattern.
    static ConstraintSettings defaultsFor(const Pattern& pattern);
};

/// Counts the interventions per rule. `skippedByLock` counts fixes that a locked dimension prevented.
struct ConstraintReport {
    size_t channelsReassigned = 0;
    size_t velocityClamped = 0;
    size_t pitchSnapped = 0;
    size_t registerFolded = 0;
    size_t gridSnapped = 0;
    size_t clippedToEnd = 0;
    size_t tiesMerged = 0;
    size_t trimmedOverlaps = 0;
    size_t slidesRemoved = 0;
    size_t droppedNotes = 0;
    size_t skippedByLock = 0;
    uint32_t passes = 0;
    bool converged = true;

    /// All interventions except `skippedByLock`.
    size_t total() const;
};

/// SPEC 4.2: snaps pitches to the scale and the sounding chord (chord-scale principle, D-57), folds them into the
/// register, puts note starts on the grid, keeps lengths inside the pattern, removes overlaps of equal pitches, keeps
/// the bass monophonic, merges slides onto the same pitch into ties, removes slides from chords and ratchets and
/// gives every voice its own MIDI channel.
///
/// Deterministic (no randomness). Runs its rules repeatedly until nothing changes, so a second call is a no-op.
/// Locked dimensions (voice and note level) are never changed (D-85); a fix that would need one is skipped.
ConstraintReport applyConstraints(Pattern& pattern, const ConstraintSettings& settings);

} // namespace mm::core
