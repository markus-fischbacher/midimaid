#pragma once

#include "core/Pattern.h"
#include "core/Register.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mm::core {

/// Effective pitch range of one voice as MIDI numbers (SPEC 4.2), including the octave offset of the voice
/// (see `effectiveRange` in `core/Register.h`).
struct VoiceConstraints {
    int rangeLow = 28;
    int rangeHigh = 52;
    bool ignoresKick = false; ///< the archetype may start on and hold over kicks (`long_tied`, STYLES.md 3)
};

struct ConstraintSettings {
    /// One entry per voice of the pattern, same order. Missing entries fall back to the role defaults.
    std::vector<VoiceConstraints> voices;
    uint32_t gridTicks = 240;          ///< note starts snap to this grid; 0 = off
    bool acceptTriplets = false;       ///< starts on the 16th-triplet grid (160 ticks) are kept
    uint32_t minNoteTicks = 60;        ///< shorter notes are dropped (1/64); the SPEC minimum of one grid step would
                                       ///< forbid the 50-75 % notes of the rolling bass archetypes (D-101)
    int chromaticPercent = 0;          ///< share of notes that may leave the scale (SPEC 3.8)
    uint32_t kickClearanceTicks = 120; ///< bass notes end this long before the next kick; 0 = off (1/32 = 120)
    uint32_t slideOverlapTicks = 60;   ///< how far a slide reaches into the next note (1/64)
    bool harshStyle = false;           ///< Hard/Industrial: tense intervals are always allowed (STYLES.md 1.8)

    /// The effective ranges of every voice of the pattern for the default profile (STYLES.md 1.6: bass 28-52, melody
    /// 55-88, stabs from `Pattern::voicing`), moved by the octave offset of each voice.
    static ConstraintSettings defaultsFor(const Pattern& pattern);

    /// The same for the ranges of a style profile.
    static ConstraintSettings forPattern(const Pattern& pattern, const RegisterProfile& profile);
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
    size_t kickShifted = 0;
    size_t clearanceTrimmed = 0;
    size_t kickSlidesRemoved = 0;
    size_t intervalsFixed = 0;
    size_t skippedByLock = 0;
    uint32_t passes = 0;
    bool converged = true;

    /// All interventions except `skippedByLock`.
    size_t total() const;
};

/// SPEC 4.2 (and STYLES.md 1.2, 1.6-1.8): snaps pitches to the scale and the sounding chord (chord-scale principle,
/// D-57), folds them into the register, puts note starts on the grid, keeps lengths inside the pattern, removes
/// overlaps of equal pitches, keeps the bass monophonic, merges slides onto the same pitch into ties, removes slides
/// from chords and ratchets and gives every voice its own MIDI channel. For bass voices it keeps notes off the kick
/// steps (shifted by 16ths, dropped on a collision or at the pattern end), ends them before the next kick (kick
/// clearance, which also beats slides) and, for all other voices, keeps tense intervals and low registers away from the
/// bass on strong steps.
///
/// Deterministic (no randomness). Runs its rules repeatedly until nothing changes, so a second call is a no-op.
/// Locked dimensions (voice and note level) are never changed (D-85); a fix that would need one is skipped.
ConstraintReport applyConstraints(Pattern& pattern, const ConstraintSettings& settings);

} // namespace mm::core
