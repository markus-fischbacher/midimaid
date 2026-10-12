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

namespace {

/// The swing the odd 16ths of a table show: their mean delay in percent above 50 (relative to a pair of 16ths), rounded
/// half away from zero, a value between 0.50 and 0.75.
float swingOfOffsets(const std::vector<int64_t>& offsets) {
    int64_t sum = 0;
    int64_t count = 0;
    for (size_t i = 1; i < offsets.size(); i += 2) {
        sum += offsets[i];
        ++count;
    }
    int64_t extra = 0;
    if (count > 0) {
        const int64_t scaled = sum * 100;
        const int64_t divisor = count * kPairTicks;
        const int64_t magnitude = (std::abs(scaled) + divisor / 2) / divisor;
        extra = scaled < 0 ? -magnitude : magnitude;
    }
    return static_cast<float>(std::clamp<int64_t>(50 + extra, 50, 75)) / 100.0f;
}

} // namespace

EffectiveGroove effectiveGroove(const Pattern& pattern, const Track& track, const GrooveLibrary* library) {
    EffectiveGroove result;
    result.swing = track.groove.swing;
    const int64_t amount = std::clamp(toPermille(track.groove.amount), 0, 1000);
    const bool wantsReference = std::string_view(track.groove.templateId) == kGrooveDrumReference;
    if (wantsReference && pattern.rhythmRef.has_value() && pattern.rhythmRef->bars >= 1) {
        const RhythmReference& ref = *pattern.rhythmRef;
        const size_t steps = std::min<size_t>(static_cast<size_t>(ref.bars) * 16, ref.timingOffsetTicks.size());
        std::vector<int64_t> offsets;
        for (size_t i = 0; i < steps; ++i) {
            offsets.push_back(scaleByPermille(ref.timingOffsetTicks[i], amount));
        }
        result.controlActive = false;
        result.swing = swingOfOffsets(offsets);
        return result;
    }
    if (library != nullptr && !wantsReference) {
        if (const GrooveTemplate* groove = library->find(track.groove.templateId); groove != nullptr && groove->swing) {
            std::vector<int64_t> offsets;
            for (const int32_t offset : groove->offsetTicks) {
                offsets.push_back(scaleByPermille(offset, amount));
            }
            result.controlActive = false;
            result.swing = swingOfOffsets(offsets);
        }
    }
    return result;
}

bool bassSwingMayFlam(VoiceRole role, float swing) {
    return role == VoiceRole::Bass && toPermille(swing) > 560;
}

} // namespace mm::core
