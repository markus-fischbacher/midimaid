#include "core/EditGrid.h"

#include "core/Pattern.h"

#include <algorithm>

namespace mm::core {

bool isValidDivision(uint32_t division) {
    return division == 4 || division == 8 || division == 16 || division == 32;
}

uint32_t EditGrid::ticks() const {
    const uint32_t step = kTicksPerBar / (isValidDivision(division) ? division : 16);
    return triplet ? step * 2 / 3 : step;
}

uint32_t snapTick(uint32_t tick, const EditGrid& grid) {
    const uint32_t step = grid.ticks();
    const uint32_t below = tick / step * step;
    return tick - below > step - (tick - below) ? below + step : below;
}

uint8_t snapPitch(int pitch, PitchSnap mode, PitchClass root, const Scale& scale) {
    pitch = std::clamp(pitch, 0, 127);
    if (mode == PitchSnap::Chromatic) {
        return static_cast<uint8_t>(pitch);
    }
    const auto inScale = [&](int candidate) {
        if (candidate < 0 || candidate > 127) {
            return false;
        }
        const auto degree = static_cast<uint8_t>(((candidate - root) % 12 + 12) % 12);
        return std::find(scale.intervals.begin(), scale.intervals.end(), degree) != scale.intervals.end();
    };
    for (int distance = 0; distance <= 12; ++distance) {
        if (inScale(pitch - distance)) {
            return static_cast<uint8_t>(pitch - distance);
        }
        if (inScale(pitch + distance)) {
            return static_cast<uint8_t>(pitch + distance);
        }
    }
    return static_cast<uint8_t>(pitch);
}

} // namespace mm::core
