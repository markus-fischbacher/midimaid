#pragma once

#include "core/Random.h"

#include <cstddef>

namespace mm::core {

/// The two macro controls (SPEC 3.3, 7.6) in one place: every value the generators derive from the energy or the
/// creativity (both 0-100 %, values outside are clamped). Integer arithmetic only, so every platform agrees.
/// Not listed here because they have their own module: the density bands (`densityBand`, Quality) and the velocity
/// level (`applyEnergy`, VelocityContour).

// -- energy: density, accents, octave jumps, pauses (STYLES.md 1.5) ------------------------------------------------

/// Share of the steps of a 16th pattern that play, in permille: 40 % at energy 0 up to 100 % at energy 100.
int energyKeptPermille(int energyPct);

/// Steps of `total` that play at this energy (rounded up, so that the density stays inside the band, D-111).
size_t energyKeptSteps(size_t total, int energyPct);

/// Chance (percent) of an accent on a bass note; aggressive archetypes accent more.
int bassAccentChance(int energyPct, bool aggressive);

/// Chance (percent) of an octave jump on a bass note.
int bassJumpChance(int energyPct);

/// Chance (percent) of an accent on a chord stab (aggressive stabs have a fixed chance of 60 %).
int stabAccentChance(int energyPct, bool aggressive);

/// Chance (percent) that a pluck sequence runs on a 16th grid instead of an 8th grid.
int pluckSixteenthChance(int energyPct);

/// Chance (percent) of an accent on an acid note.
int acidAccentChance(int energyPct);

/// Chance (percent) of a slide between two acid notes.
int acidSlideChance(int energyPct);

/// Notes per bar of the atonal cell (2-4) and hits per cell of `sparse_hits` (1-4).
int atonalNoteCount(int energyPct);
int sparseHitCount(int energyPct);

/// Weight factor (percent) of an archetype of its density class in the automatic choice: dense archetypes gain with
/// the energy, calm ones lose.
int archetypeDensityFactor(bool dense, bool calm, int energyPct);

// -- creativity: variations, archetype choice, unusual intervals (SPEC 7.6) ----------------------------------------

/// Chance (percent) that the variation bar of a phrase actually varies: the creativity, scaled by the energy
/// (60 % of it at energy 0, 100 % at energy 50, 140 % at energy 100), at most 100.
int variationChance(int creativityPct, int energyPct);

/// Range (percent) by which the weights of the archetypes scatter around the profile: each weight is multiplied by a
/// factor between `100 - range` and `100 + range` percent. 0 at creativity 0 (no draw).
int archetypeScatterRange(int creativityPct);

/// The factor (percent) by which one archetype weight is multiplied: uniform from `100 - range` to `100 + range`
/// (at least 1), drawn from `rng`. Without a range nothing is drawn and the factor is 100.
int archetypeScatterFactor(int range, Pcg32& rng);

/// Share of the chromatic budget that is used (the budget itself is the limit of the constraint layer): half of it at
/// creativity 0, all of it at 100, rounded up.
size_t chromaticFill(size_t budget, int creativityPct);

} // namespace mm::core
