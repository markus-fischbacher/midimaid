#include "core/EvolveLayer.h"

#include "core/Random.h"
#include "core/Variation.h"

#include <algorithm>

namespace mm::core {

EvolveSettings clampEvolveSettings(EvolveSettings settings) {
    settings.everyPasses = std::clamp(settings.everyPasses, kEvolveMinPasses, kEvolveMaxPasses);
    settings.strengthPct = std::clamp(settings.strengthPct, 1, 100);
    return settings;
}

void EvolveLayer::start(size_t slotIndex, Pattern base, uint64_t seed) {
    slot_ = slotIndex;
    base_ = base;
    stages_.clear();
    stages_.push_back(std::move(base));
    seed_ = seed;
    stageNumber_ = 0;
}

void EvolveLayer::stop() {
    slot_.reset();
    stages_.clear();
    base_ = Pattern{};
    stageNumber_ = 0;
}

bool EvolveLayer::advance(const Pattern& slotPattern, const StyleProfile& style, const ArchetypeSettings& archetype,
                          const EvolveSettings& settings) {
    if (!active()) {
        return false;
    }
    const EvolveSettings use = clampEvolveSettings(settings);
    Pattern next = use.fromOriginal ? slotPattern : stages_.back();
    VariationRequest request;
    request.seed = deriveSeed(seed_, static_cast<uint64_t>(stageNumber_) + 1);
    request.strengthPct = use.strengthPct;
    if (applyVariation(next, style, archetype, request) == 0) {
        return false;
    }
    stages_.push_back(std::move(next));
    while (stages_.size() > kEvolveBackStages + 1) {
        stages_.pop_front();
    }
    ++stageNumber_;
    return true;
}

bool EvolveLayer::back() {
    if (!canBack()) {
        return false;
    }
    stages_.pop_back();
    return true;
}

} // namespace mm::core
