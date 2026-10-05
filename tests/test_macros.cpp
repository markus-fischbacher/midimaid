#include "core/Macros.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

using namespace mm::core;

TEST_CASE("the share of playing steps runs from 40 % to 100 % with the energy", "[macros]") {
    CHECK(energyKeptPermille(0) == 400);
    CHECK(energyKeptPermille(50) == 700);
    CHECK(energyKeptPermille(100) == 1000);
    CHECK(energyKeptPermille(-5) == 400);
    CHECK(energyKeptPermille(150) == 1000);
    CHECK(energyKeptSteps(16, 0) == 7);   // 6.4, rounded up
    CHECK(energyKeptSteps(16, 50) == 12); // 11.2
    CHECK(energyKeptSteps(16, 100) == 16);
    CHECK(energyKeptSteps(3, 100) == 3);
    CHECK(energyKeptSteps(0, 50) == 0);
}

TEST_CASE("accent, jump and slide chances follow the energy", "[macros]") {
    CHECK(bassAccentChance(0, false) == 30);
    CHECK(bassAccentChance(100, false) == 90);
    CHECK(bassAccentChance(0, true) == 50);
    CHECK(bassAccentChance(100, true) == 100);
    CHECK(bassAccentChance(50, true) == 75);
    CHECK(bassJumpChance(0) == 5);
    CHECK(bassJumpChance(50) == 12);
    CHECK(bassJumpChance(100) == 20);
    CHECK(stabAccentChance(0, false) == 20);
    CHECK(stabAccentChance(100, false) == 50);
    CHECK(stabAccentChance(0, true) == 60);
    CHECK(stabAccentChance(100, true) == 60);
    CHECK(pluckSixteenthChance(0) == 30);
    CHECK(pluckSixteenthChance(50) == 50);
    CHECK(pluckSixteenthChance(100) == 70);
    CHECK(acidAccentChance(0) == 30);
    CHECK(acidAccentChance(100) == 70);
    CHECK(acidSlideChance(0) == 25);
    CHECK(acidSlideChance(50) == 35);
    CHECK(acidSlideChance(100) == 45);
}

TEST_CASE("the cells of the tension archetypes grow with the energy", "[macros]") {
    CHECK(atonalNoteCount(0) == 2);
    CHECK(atonalNoteCount(49) == 2);
    CHECK(atonalNoteCount(50) == 3);
    CHECK(atonalNoteCount(100) == 4);
    CHECK(sparseHitCount(0) == 1);
    CHECK(sparseHitCount(34) == 2);
    CHECK(sparseHitCount(50) == 2);
    CHECK(sparseHitCount(100) == 4);
}

TEST_CASE("dense archetypes gain with the energy, calm ones lose", "[macros]") {
    CHECK(archetypeDensityFactor(true, false, 0) == 50);
    CHECK(archetypeDensityFactor(true, false, 100) == 150);
    CHECK(archetypeDensityFactor(false, true, 0) == 150);
    CHECK(archetypeDensityFactor(false, true, 100) == 50);
    CHECK(archetypeDensityFactor(false, false, 0) == 100);
    CHECK(archetypeDensityFactor(false, false, 100) == 100);
}

TEST_CASE("the variation chance is the creativity scaled by the energy", "[macros]") {
    CHECK(variationChance(40, 50) == 40); // the defaults leave the creativity as it is
    CHECK(variationChance(40, 0) == 24);
    CHECK(variationChance(40, 100) == 56);
    CHECK(variationChance(100, 0) == 60);
    CHECK(variationChance(100, 50) == 100);
    CHECK(variationChance(100, 100) == 100); // capped
    CHECK(variationChance(0, 100) == 0);
    CHECK(variationChance(-10, 100) == 0);
    CHECK(variationChance(200, 0) == 60);
}

TEST_CASE("the archetype weights scatter by the creativity", "[macros]") {
    CHECK(archetypeScatterRange(0) == 0);
    CHECK(archetypeScatterRange(40) == 40);
    CHECK(archetypeScatterRange(100) == 100);
    CHECK(archetypeScatterRange(-5) == 0);
    CHECK(archetypeScatterRange(200) == 100);
}

TEST_CASE("the scatter factor is uniform within the range and draws nothing without one", "[macros]") {
    Pcg32 untouched = Pcg32::fromSeed(5);
    Pcg32 rng = Pcg32::fromSeed(5);
    CHECK(archetypeScatterFactor(0, rng) == 100);
    CHECK(archetypeScatterFactor(-3, rng) == 100);
    CHECK(rng.next() == untouched.next());
    for (const int range : {10, 40, 100}) {
        Pcg32 draws = Pcg32::fromSeed(static_cast<uint64_t>(range));
        int low = 1000;
        int high = 0;
        int64_t sum = 0;
        constexpr int kDraws = 20000;
        for (int i = 0; i < kDraws; ++i) {
            const int factor = archetypeScatterFactor(range, draws);
            low = std::min(low, factor);
            high = std::max(high, factor);
            sum += factor;
        }
        INFO("range " << range);
        CHECK(low == std::max(1, 100 - range));
        CHECK(high == 100 + range);
        CHECK(sum > int64_t{kDraws} * 98); // mean 100
        CHECK(sum < int64_t{kDraws} * 102);
    }
}

TEST_CASE("the creativity fills half to all of the chromatic budget", "[macros]") {
    CHECK(chromaticFill(10, 0) == 5);
    CHECK(chromaticFill(10, 100) == 10);
    CHECK(chromaticFill(1, 0) == 1); // rounded up
    CHECK(chromaticFill(1, 100) == 1);
    CHECK(chromaticFill(0, 50) == 0);
    CHECK(chromaticFill(7, 40) == 5);   // 7 * 70 / 100 = 4.9
    CHECK(chromaticFill(35, 50) == 27); // 35 * 75 / 100 = 26.25
    for (int creativity = 0; creativity <= 100; creativity += 7) {
        for (size_t budget = 0; budget <= 40; ++budget) {
            CHECK(chromaticFill(budget, creativity) <= budget);
        }
    }
}
