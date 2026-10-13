#pragma once

#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstdint>

namespace mm::core {

/// Probability and conditions in the generated patterns (STYLES.md 1.16, SPEC 3.19, D-233). The generators set them
/// sparingly: at most 4 % of the notes of a voice at energy 0, 14 % at energy 100 (below the 15 % of the style rule),
/// and only in the places the styles use them:
///  - Peak Time: ghost notes of the bass (quiet 16ths between the beats) with 50-75 %, ornaments of the melody (soft
///    notes on the off 16ths) with 60-80 %;
///  - Melodic Techno: no chances; a variation in an answer phrase (soft off-16th notes of the melody) plays only in the
///    second of every two passes (2:2);
///  - Hard/Industrial: the repeats of a ratchet and the hits of `sparse_hits` with 40-70 %.
/// Notes that already have a chance or a condition, notes with a locked rhythm and voices with a locked rhythm are left
/// alone. Draws come from an own seed stream per voice (so the rest stays as it was); the bars [fromBar, toBar) only.
void applyChances(Pattern& pattern, size_t voice, const StyleProfile& style, int energyPct, uint64_t seed,
                  uint32_t fromBar, uint32_t toBar);

/// The share of the notes of a voice that may get a chance or a condition, in percent, for an energy.
int chanceBudgetPct(int energyPct);

} // namespace mm::core
