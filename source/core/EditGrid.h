#pragma once

#include "core/Theory.h"

#include <cstdint>

namespace mm::core {

/// The edit grid of the piano roll (SPEC 3.5): 1/4 to 1/32 notes, optionally as triplets. Ticks at 960 PPQ, a bar
/// is 3840 ticks, so a 16th is 240 ticks and a 16th triplet 160.
struct EditGrid {
    uint32_t division = 16; ///< 4, 8, 16 or 32; any other value counts as 16
    bool triplet = false;

    /// Ticks between two grid points: 3840 / division, a triplet is two thirds of that.
    uint32_t ticks() const;
    bool operator==(const EditGrid&) const = default;
};

bool isValidDivision(uint32_t division);

/// The grid point nearest to `tick`; halfway between two points the earlier one wins.
uint32_t snapTick(uint32_t tick, const EditGrid& grid);

enum class PitchSnap { Chromatic, Scale };

/// Chromatic keeps the pitch (cut to 0-127). Scale gives the nearest tone of `scale` above `root`; a pitch halfway
/// between two tones snaps down.
uint8_t snapPitch(int pitch, PitchSnap mode, PitchClass root, const Scale& scale);

} // namespace mm::core
