#pragma once

#include "core/Transpose.h"

#include <cstddef>
#include <cstdint>

namespace mm::core {

constexpr uint32_t kTicksPerQuarter = 960; // SPEC 5

/// One note of a pattern. Positions are ticks (960 PPQ) from the pattern start.
/// Phase 0 primitive; replaced by the full pattern model of SPEC 5.
struct PatternNote {
    uint32_t startTick;
    uint32_t lengthTicks;
    uint8_t channel; // 1-16
    uint8_t pitch;
    uint8_t velocity;
    // Probability and conditions (SPEC 3.19, v1.1, D-233). All parts of a ratchet carry the id and the values of their
    // note, so they stay or fall together.
    uint32_t noteId = 0;        ///< the id of the note in the pattern, the key of the decision per pass
    uint8_t chance = 100;       ///< percent
    uint8_t condA = 1;          ///< plays in the A-th of every B passes
    uint8_t condB = 1;
    uint32_t slideTargetId = 0; ///< a slide note: the id of the note it glides into (0: none)
    uint32_t plainLengthTicks = 0; ///< a slide note: the length when that note does not play in a pass (0: same)
};

/// Non-owning view of an immutable pattern.
struct PatternView {
    const PatternNote* notes = nullptr;
    size_t count = 0;
    uint32_t lengthTicks = 0;
    TransposeInfo transpose{}; ///< what the live transposition needs to know about the voice (SPEC 3.16)
    uint64_t seed = 0;         ///< of the pattern: with the note id and the pass it decides which notes sound (SPEC 3.19)
};

/// Hard-coded Phase 0 pattern: one bar of offbeat bass (steps 2, 6, 10, 14), MIDI 45 (A1 with C3 = 60), accent on the last offbeat.
PatternView placeholderPattern();

} // namespace mm::core
