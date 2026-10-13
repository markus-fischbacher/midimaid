#pragma once

#include "core/GrooveLibrary.h"
#include "core/Pattern.h"

#include <cstdint>
#include <vector>

namespace mm::core {

/// Groove template ids of `GrooveSettings::templateId` (SPEC 3.9): an empty id or "straight" uses the swing control;
/// "drum_reference" takes timing and velocity from `Pattern::rhythmRef` (no double swing); any other id names a template
/// of `OutputSettings::grooves` (v1.1; an unknown one is straight).
constexpr const char* kGrooveStraight = "straight";
constexpr const char* kGrooveDrumReference = "drum_reference";

struct OutputSettings {
    uint32_t slideOverlapTicks = 60;   ///< how far a slide reaches into the next note (1/64)
    uint32_t kickClearanceTicks = 120; ///< bass notes end this long before the next kick; 0 = off
    uint8_t accentVelocity = 124;      ///< velocity of accented notes (STYLES.md 1.9)
    uint32_t minNoteTicks = 60;        ///< notes that end up shorter are dropped
    bool includeGroove = true;         ///< false skips stage 2: the notes keep their grid (export without groove)
    std::vector<bool> ignoresKick;     ///< per voice (pattern order): the archetype may hold over kicks (`long_tied`)
    int octaveShift = 0;               ///< stage 1: octaves added to every pitch (an instance's own octave, D-147)
    const GrooveLibrary* grooves = nullptr; ///< the groove templates a track may name (SPEC 3.9, v1.1); null: none
};

/// A sounding note. Positions are ticks from the pattern start; `endTick` of a slide on the last note lies behind the
/// pattern end (it glides into the first note of the next pass).
struct OutputNote {
    uint32_t noteId = 0;
    uint8_t channel = 1;
    uint8_t pitch = 0;
    uint8_t velocity = 100;
    int32_t startTick = 0;
    int32_t endTick = 0;
    bool slideIntoNext = false; ///< the note overlaps the next one by the slide overlap
    // Probability and conditions of the note (SPEC 3.19, D-233); all parts of a ratchet carry them.
    uint8_t chance = 100;
    uint8_t condA = 1;
    uint8_t condB = 1;
    uint32_t slideTargetId = 0; ///< a note that reaches into the next one: the id of that note (0: none)
    int32_t plainEndTick = 0;   ///< and where it ends when that note does not play in a pass

    bool operator==(const OutputNote&) const = default;
};

struct OutputVoice {
    VoiceRole role = VoiceRole::Bass;
    uint8_t channel = 1;
    bool muted = false;
    std::vector<OutputNote> notes; ///< sorted by start, pitch, id

    bool operator==(const OutputVoice&) const = default;
};

struct OutputPattern {
    uint32_t lengthTicks = 0;
    std::vector<OutputVoice> voices;

    bool operator==(const OutputPattern&) const = default;
};

/// Delay of an off-beat 16th for a swing value (50-75 %, relative to a pair of 16ths = 480 ticks) scaled by
/// `amount` (0-1). Integer arithmetic on the rounded per-mille values, identical on every platform.
int32_t swingOffsetTicks(float swing, float amount);

/// SPEC 4.2a, the v1.0 stages in their fixed order:
///   1. octave shift of the instance (pitches that leave 0 to 127 are folded back by octaves)
///   2. groove and swing (timing offset, velocity profile)
///   3. slide overlaps, relative to the shifted next note
///   4. kick clearance re-checked, note ends shortened (the kick stays straight)
///   5. accent velocity, ratchets resolved (a ratchet of 2-4 splits the note into equal repeats, D-227)
/// The stages 0 (probability) and the semitone transposition of stage 1 belong to v1.1 and slot in at the same places. Pure and deterministic; the pattern is not changed.
OutputPattern renderOutput(const Pattern& pattern, const OutputSettings& settings);

} // namespace mm::core
