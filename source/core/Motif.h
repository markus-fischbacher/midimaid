#pragma once

#include "core/Pattern.h"
#include "core/Random.h"
#include "core/Register.h"
#include "core/Theory.h"
#include "core/VelocityContour.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace mm::core {

/// Motif engine (STYLES.md 1.11): a motif of 1-2 bars with its own rhythm comes first, it is repeated before it is
/// varied (A A A A'), weak steps carry passing and neighbour tones, pickup notes are allowed. Motifs are written in
/// scale steps, so they can be transposed and moved to other chords; `realizeMotif` turns them into notes.

constexpr int kStepsPerBar = 16;

struct MotifNote {
    uint8_t startStep = 0;      ///< 16th steps from the motif start (0 to 16 * bars - 1)
    uint32_t lengthTicks = 240; ///< multiple of 1/64 (60 ticks), at least 60
    int8_t degree = 0;          ///< scale steps from the anchor pitch (0 = anchor, negative = below)
    bool accent = false;
    int8_t velocityOffset = 0; ///< added to the base velocity (-20 to +20)

    bool operator==(const MotifNote&) const = default;
};

struct Motif {
    uint8_t bars = 1;             ///< 1 or 2
    std::vector<MotifNote> notes; ///< sorted by start step

    bool operator==(const Motif&) const = default;
};

/// What a motif has to satisfy. `basePitch` is the MIDI pitch of scale step 0 (a pitch of the key's root, SPEC 7.3);
/// strong steps (0, 4, 8, 12 of every bar) need tones of `referenceChord`.
struct MotifRules {
    const Scale* scale = nullptr;
    PitchClass keyRoot = 9;
    Chord referenceChord{0, ChordQuality::Minor};
    int basePitch = 57;
    Range range = kDefaultMelodyRange;
    int maxSpanSemitones = 7; ///< highest minus lowest pitch of the motif
};

struct MotifParams {
    uint8_t bars = 1;
    int minNotes = 3;
    int maxNotes = 6;
    bool allowPickup = true;
    int pickupPercent = 15; ///< chance of 1-2 pickup notes on the last steps before the bar line
};

/// Realised MIDI pitch of a scale step (any integer, octaves wrap). The scale must not be empty.
int scaleStepToMidi(const Scale& scale, int basePitch, int step);

/// The rules of STYLES.md 1.11 and the bounds of the motif: notes sorted, inside the motif, no overlap, all pitches in
/// the range and within the span, strong steps on chord tones, and at least 20 % of the notes on weak steps are
/// non-chord tones that are approached and left by one scale step (passing or neighbour tones).
bool isValidMotif(const Motif& motif, const MotifRules& rules);

/// Notes on weak steps (step % 4 != 0) and the non-chord tones among them.
size_t weakStepCount(const Motif& motif);
size_t weakStepNonChordCount(const Motif& motif, const MotifRules& rules);

/// A new motif that satisfies `isValidMotif`; nullopt if the rules cannot be met with the parameters (after a bounded
/// number of attempts). Deterministic per generator state.
std::optional<Motif> generateMotif(const MotifParams& params, const MotifRules& rules, Pcg32& rng);

enum class RepetitionStyle {
    Standard, ///< 2-4 repetitions (typically 3), then a variation, repeated over the phrase
    Hypnotic  ///< every fourth occurrence varies (micro variation), `hypnotic_motif`
};

/// Which of `blocks` consecutive motif occurrences are varied. The first variation never comes before the second
/// repetition; with few blocks nothing varies (the loop itself repeats).
std::vector<bool> planRepetitions(uint32_t blocks, RepetitionStyle style, Pcg32& rng);

enum class MotifVariation {
    Micro,     ///< one note changes its length, accent or velocity
    Transpose, ///< all notes by 1-2 scale steps, strong steps repaired to the nearest chord tone
    Invert,    ///< mirrored around the first note, strong steps repaired
    NewEnding, ///< the last note moves to another chord tone
    ShiftNote  ///< one note moves by one step
};

/// A variation that differs from the motif and satisfies `isValidMotif`; nullopt if none was found.
std::optional<Motif> varyMotif(const Motif& motif, MotifVariation kind, const MotifRules& rules, Pcg32& rng);

/// The motif for every block of a phrase of `totalBars` bars: the plan of `planRepetitions` with variations at the
/// planned places (Hypnotic: micro variations; Standard: micro, shifted note, new ending, transposition, inversion).
/// A variation that cannot be found leaves the block unchanged. Empty if the phrase is shorter than the motif or the
/// bars do not divide.
std::vector<Motif> buildMotifSequence(const Motif& motif, const MotifRules& rules, uint32_t totalBars,
                                      RepetitionStyle style, Pcg32& rng);

/// Share of blocks that equal the first block, in permille (0 for an empty sequence).
uint32_t repetitionPermille(const std::vector<Motif>& sequence);

/// Notes of a motif placed at `startTick`. The scale steps shift by `stepShift` (to follow a chord); pitches outside
/// the range move by octaves into it. Note ids stay 0, the caller allocates them. Velocity is 100 plus the offset.
std::vector<Note> realizeMotif(const Motif& motif, const MotifRules& rules, int stepShift, uint32_t startTick);

} // namespace mm::core
