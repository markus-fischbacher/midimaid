#include "core/KickGrid.h"

#include <array>

namespace mm::core {

namespace {

constexpr uint32_t kStepTicks = 240; // a 16th at 960 PPQ

constexpr std::array<uint8_t, 4> kFourOnTheFloor{0, 4, 8, 12};
constexpr std::array<uint8_t, 5> kFourOnTheFloorPickup{0, 4, 8, 12, 15};
constexpr std::array<uint8_t, 2> kHalftime{0, 8};
constexpr std::array<uint8_t, 4> kBrokenA{0, 6, 8, 12};
constexpr std::array<uint8_t, 4> kBrokenB{0, 4, 10, 12};

const std::array<KickGrid, 5> kGrids{{
    {"4otf", kFourOnTheFloor},
    {"4otf_pickup", kFourOnTheFloorPickup},
    {"halftime", kHalftime},
    {"broken_a", kBrokenA},
    {"broken_b", kBrokenB},
}};

std::string_view gridIdForBar(const Pattern& pattern, uint32_t bar) {
    for (const Phrase& phrase : pattern.phrases) {
        if (bar >= phrase.startBar && bar < phrase.startBar + phrase.lengthBars && phrase.kickGridId.has_value()) {
            return *phrase.kickGridId;
        }
    }
    return pattern.kickGridId;
}

} // namespace

const KickGrid* findKickGrid(std::string_view id) {
    for (const KickGrid& grid : kGrids) {
        if (grid.id == id) {
            return &grid;
        }
    }
    return nullptr;
}

std::vector<uint32_t> kickTicks(const Pattern& pattern) {
    std::vector<uint32_t> ticks;
    for (uint32_t bar = 0; bar < pattern.lengthBars; ++bar) {
        const std::string_view id = gridIdForBar(pattern, bar);
        const uint32_t barStart = bar * kTicksPerBar;
        if (id == "custom" && pattern.rhythmRef.has_value() && pattern.rhythmRef->bars >= 1) {
            const RhythmReference& ref = *pattern.rhythmRef;
            const uint32_t refBar = bar % ref.bars;
            for (uint32_t step = 0; step < 16; ++step) {
                if (ref.kickSteps.test(refBar * 16 + step)) {
                    ticks.push_back(barStart + step * kStepTicks);
                }
            }
            continue;
        }
        const KickGrid* grid = findKickGrid(id);
        if (grid == nullptr) {
            grid = findKickGrid("4otf");
        }
        for (const uint8_t step : grid->steps) {
            ticks.push_back(barStart + step * kStepTicks);
        }
    }
    return ticks;
}

} // namespace mm::core
