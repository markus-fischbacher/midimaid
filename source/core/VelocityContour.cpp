#include "core/VelocityContour.h"

#include <algorithm>

namespace mm::core {

bool isValidContour(const VelocityContour& contour) {
    return std::all_of(contour.steps.begin(), contour.steps.end(), [](uint8_t v) { return v >= 1 && v <= 127; });
}

VelocityContour applyEnergy(const VelocityContour& contour, uint16_t energyPermille) {
    const int energy = std::min<int>(energyPermille, kMaxEnergyPermille);
    // (energy - 500) * 12 / 500, rounded half away from zero so that the shift is symmetric around energy 0.5.
    const int scaled = (energy - 500) * kEnergyVelocitySpan;
    const int shift = (scaled >= 0 ? scaled + 250 : scaled - 250) / 500;

    VelocityContour result;
    for (size_t i = 0; i < kContourSteps; ++i) {
        result.steps[i] = static_cast<uint8_t>(std::clamp(int(contour.steps[i]) + shift, 1, 127));
    }
    return result;
}

size_t applyContour(Track& track, const VelocityContour& contour) {
    if (track.lock.velocity) {
        return 0;
    }
    size_t changed = 0;
    for (Note& note : track.notes) {
        if (note.lock.velocity) {
            continue;
        }
        const uint8_t velocity = contour.velocityAtTick(note.startTick);
        if (note.velocity != velocity) {
            note.velocity = velocity;
            ++changed;
        }
    }
    return changed;
}

size_t detectAccents(Track& track, uint8_t threshold, uint8_t baseVelocity) {
    size_t found = 0;
    for (Note& note : track.notes) {
        if (isAccentVelocity(note.velocity, threshold)) {
            note.accent = true;
            note.velocity = baseVelocity;
            ++found;
        }
    }
    return found;
}

} // namespace mm::core
