#pragma once

#include "core/Pattern.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mm::core {

/// Velocity contour of STYLES.md 1.9: the base velocity per 16th step of one bar, repeated for every bar of a pattern.
/// Accents are not part of the contour; they are a flag of the note and get the accent velocity in the output stage.

constexpr uint32_t kTicksPerStep = 240; ///< one 16th at 960 PPQ
constexpr size_t kContourSteps = 16;

/// Energy is handled in permille (0-1000), like swing, so the result never depends on floating point.
constexpr uint16_t kMaxEnergyPermille = 1000;
constexpr int kEnergyVelocitySpan = 12; ///< velocity shift at energy 0 (-12) and 1 (+12); energy 0.5 changes nothing

struct VelocityContour {
    std::array<uint8_t, kContourSteps> steps;

    VelocityContour() { steps.fill(100); }

    /// Velocity of a step; steps beyond one bar wrap around (multi-bar patterns repeat the contour).
    uint8_t velocityAt(uint32_t step) const { return steps[step % kContourSteps]; }

    /// Velocity at a tick: the nearest 16th step, a tick halfway between two steps belongs to the later one.
    uint8_t velocityAtTick(uint32_t tick) const { return velocityAt((tick + kTicksPerStep / 2) / kTicksPerStep); }

    bool operator==(const VelocityContour&) const = default;
};

/// Every step must lie in 1-127.
bool isValidContour(const VelocityContour& contour);

/// Scales the velocity level of a contour with the energy (STYLES.md 1.5): linear from -12 at energy 0 to +12 at
/// energy 1000, all steps move by the same amount (the shape stays), results are limited to 1-127.
/// Energy values above 1000 count as 1000.
VelocityContour applyEnergy(const VelocityContour& contour, uint16_t energyPermille);

/// Sets the base velocity of the notes from the contour (nearest step of the note start). Notes with a locked
/// velocity (voice or note level) stay. Returns the number of notes whose velocity changed.
size_t applyContour(Track& track, const VelocityContour& contour);

/// Accent settings (STYLES.md 1.9). The output velocity of accents lives in `OutputSettings::accentVelocity`; the
/// threshold only serves the import of foreign MIDI.
struct AccentSettings {
    uint8_t accentVelocity = 124;
    uint8_t importThreshold = 110;

    /// 1 <= threshold <= accent velocity <= 127
    bool isValid() const { return importThreshold >= 1 && importThreshold <= accentVelocity && accentVelocity <= 127; }

    bool operator==(const AccentSettings&) const = default;
};

/// An imported note is an accent if its velocity reaches the threshold.
constexpr bool isAccentVelocity(uint8_t velocity, uint8_t threshold) {
    return velocity >= threshold;
}

/// Import: marks notes at or above the threshold as accents and resets their base velocity to `baseVelocity`, so a
/// note that loses its accent later sounds normal. Returns the number of accents found.
size_t detectAccents(Track& track, uint8_t threshold, uint8_t baseVelocity = 100);

} // namespace mm::core
