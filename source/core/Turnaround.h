#pragma once

#include "core/Pattern.h"
#include "core/Random.h"

#include <vector>

namespace mm::core {

/// Turnaround (STYLES.md 1.12, D-226): the last bar of a phrase of 8 or 16 bars varies. The chance of a phrase to get
/// one rises with the energy (10 % at energy 0, 70 % at 100; starting values for the listening test).
int turnaroundChancePct(int energyPct);

/// Draws the turnaround flag of every phrase of 8 or 16 bars from its own seed stream (not from the generator's
/// stream, so that everything else stays as it was). Phrases of 4 bars never get one; flags that are already set stay.
void assignTurnarounds(std::vector<Phrase>& phrases, int energyPct, uint64_t seed);

/// How the last bar of one voice varies.
enum class TurnaroundKind {
    Pause,      ///< the last two beats fall silent
    Fill,       ///< a run of 16th notes over the last beat, rising in velocity
    OctaveJump, ///< the notes of the last two beats move an octave
    Ratchet,    ///< one late note of the bar is repeated 2-4 times (STYLES.md 1.13); the bar's only ratchet
};

/// The kind drawn for a voice: energy favours fills, jumps and ratchets, low energy the pause. Voices with chords (several notes
/// on one start) never get a fill, a run would turn them into a line.
TurnaroundKind chooseTurnaroundKind(bool chordal, int energyPct, Pcg32& rng);

/// Applies the turnaround to the last bar of `phrase` in `pattern` (absolute ticks) for every voice with
/// `varies[i]` true (an empty list: all voices). Locked rhythm or pitch of a voice keeps it as it is. New notes get
/// ids from the pattern. The constraint layer runs afterwards.
void applyTurnaround(Pattern& pattern, const Phrase& phrase, int energyPct, uint64_t seed,
                     const std::vector<bool>& varies = {});

} // namespace mm::core
