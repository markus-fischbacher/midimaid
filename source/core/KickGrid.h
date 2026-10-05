#pragma once

#include "core/Pattern.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mm::core {

/// A kick pattern within one bar: the 16th steps (0-15) on which the kick plays (STYLES.md 1.2).
struct KickGrid {
    std::string_view id;
    std::span<const uint8_t> steps;
};

/// The fixed grids `4otf`, `4otf_pickup`, `halftime`, `broken_a`, `broken_b`; nullptr for any other id (including
/// `custom`, which comes from the drum reference).
const KickGrid* findKickGrid(std::string_view id);

/// Absolute kick positions (ticks, ascending) inside the pattern. Every bar uses the grid of the phrase covering it
/// (`Phrase::kickGridId`) or else `Pattern::kickGridId`. `custom` reads the kick steps of the drum reference
/// (repeated every `bars` bars); `custom` without a reference and unknown ids fall back to `4otf`.
std::vector<uint32_t> kickTicks(const Pattern& pattern);

} // namespace mm::core
