#pragma once

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
};

/// Non-owning view of an immutable pattern.
struct PatternView {
    const PatternNote* notes = nullptr;
    size_t count = 0;
    uint32_t lengthTicks = 0;
};

/// Hard-coded Phase 0 pattern: one bar of offbeat bass (steps 2, 6, 10, 14), MIDI 45 (A1 with C3 = 60), accent on the last offbeat.
PatternView placeholderPattern();

} // namespace mm::core
