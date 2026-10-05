#include "core/Groove.h"

#include "core/OutputStage.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace mm::core {

namespace {

constexpr int64_t kPairTicks = 480; // two 16ths

int32_t toPermille(float value) {
    return static_cast<int32_t>(std::lround(value * 1000.0f));
}

/// value * permille / 1000, rounded half away from zero (as the output stage does).
int64_t scaleByPermille(int64_t value, int64_t permille) {
    const int64_t magnitude = (std::abs(value) * permille + 500) / 1000;
    return value < 0 ? -magnitude : magnitude;
}

} // namespace

void applyStyleGroove(Pattern& pattern, const StyleProfile& style) {
    for (Track& track : pattern.voices) {
        track.groove.swing = track.role == VoiceRole::Bass ? style.bass.swingDefault : style.melody.swingDefault;
        track.groove.amount = 1.0f;
        track.groove.templateId = kGrooveStraight;
    }
}

EffectiveGroove effectiveGroove(const Pattern& pattern, const Track& track) {
    EffectiveGroove result;
    result.swing = track.groove.swing;
    const bool wantsReference = std::string_view(track.groove.templateId) == kGrooveDrumReference;
    if (!wantsReference || !pattern.rhythmRef.has_value() || pattern.rhythmRef->bars < 1) {
        return result;
    }
    const RhythmReference& ref = *pattern.rhythmRef;
    const int64_t amount = std::clamp(toPermille(track.groove.amount), 0, 1000);
    const size_t steps = std::min<size_t>(static_cast<size_t>(ref.bars) * 16, ref.timingOffsetTicks.size());
    int64_t sum = 0;
    int64_t count = 0;
    for (size_t i = 1; i < steps; i += 2) {
        sum += scaleByPermille(ref.timingOffsetTicks[i], amount);
        ++count;
    }
    // percent above 50: mean delay * 100 / 480, rounded half away from zero
    int64_t extra = 0;
    if (count > 0) {
        const int64_t scaled = sum * 100;
        const int64_t divisor = count * kPairTicks;
        const int64_t magnitude = (std::abs(scaled) + divisor / 2) / divisor;
        extra = scaled < 0 ? -magnitude : magnitude;
    }
    result.controlActive = false;
    result.swing = static_cast<float>(std::clamp<int64_t>(50 + extra, 50, 75)) / 100.0f;
    return result;
}

bool bassSwingMayFlam(VoiceRole role, float swing) {
    return role == VoiceRole::Bass && toPermille(swing) > 560;
}

} // namespace mm::core
