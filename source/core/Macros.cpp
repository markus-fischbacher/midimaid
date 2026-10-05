#include "core/Macros.h"

#include <algorithm>

namespace mm::core {

namespace {

int clampPct(int pct) {
    return std::clamp(pct, 0, 100);
}

} // namespace

int energyKeptPermille(int energyPct) {
    return 400 + 6 * clampPct(energyPct);
}

size_t energyKeptSteps(size_t total, int energyPct) {
    return std::min(total, (total * static_cast<size_t>(energyKeptPermille(energyPct)) + 999) / 1000);
}

int bassAccentChance(int energyPct, bool aggressive) {
    const int energy = clampPct(energyPct);
    return aggressive ? 50 + energy / 2 : 30 + (6 * energy) / 10;
}

int bassJumpChance(int energyPct) {
    return 5 + (15 * clampPct(energyPct)) / 100;
}

int stabAccentChance(int energyPct, bool aggressive) {
    return aggressive ? 60 : 20 + (3 * clampPct(energyPct)) / 10;
}

int pluckSixteenthChance(int energyPct) {
    return 30 + (4 * clampPct(energyPct)) / 10;
}

int acidAccentChance(int energyPct) {
    return 30 + (4 * clampPct(energyPct)) / 10;
}

int acidSlideChance(int energyPct) {
    return 25 + (2 * clampPct(energyPct)) / 10;
}

int atonalNoteCount(int energyPct) {
    return 2 + (2 * clampPct(energyPct)) / 100;
}

int sparseHitCount(int energyPct) {
    return 1 + (3 * clampPct(energyPct)) / 100;
}

int archetypeDensityFactor(bool dense, bool calm, int energyPct) {
    const int energy = clampPct(energyPct);
    if (dense) {
        return 50 + energy;
    }
    return calm ? 150 - energy : 100;
}

int variationChance(int creativityPct, int energyPct) {
    return std::min(100, clampPct(creativityPct) * (60 + (8 * clampPct(energyPct)) / 10) / 100);
}

int archetypeScatterRange(int creativityPct) {
    return clampPct(creativityPct);
}

int archetypeScatterFactor(int range, Pcg32& rng) {
    if (range <= 0) {
        return 100;
    }
    return std::max(1, 100 - range + static_cast<int>(rng.bounded(static_cast<uint32_t>(2 * range + 1))));
}

size_t chromaticFill(size_t budget, int creativityPct) {
    return (budget * static_cast<size_t>(50 + clampPct(creativityPct) / 2) + 99) / 100;
}

} // namespace mm::core
