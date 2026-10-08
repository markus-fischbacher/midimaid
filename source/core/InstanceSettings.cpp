#include "core/InstanceSettings.h"

#include <algorithm>

namespace mm::core {

std::string_view toString(InstanceRole role) {
    switch (role) {
    case InstanceRole::Hub:
        return "hub";
    case InstanceRole::Voice:
        return "voice";
    case InstanceRole::Solo:
        break;
    }
    return "solo";
}

std::string_view toString(OutputMode mode) {
    return mode == OutputMode::None ? "none" : "one_voice";
}

std::string_view toString(SlotFollow follow) {
    return follow == SlotFollow::Own ? "own" : "hub";
}

SlotFollow parseSlotFollow(std::string_view text) {
    return text == "own" ? SlotFollow::Own : SlotFollow::Hub;
}

InstanceRole parseRole(std::string_view text) {
    if (text == "hub") {
        return InstanceRole::Hub;
    }
    if (text == "voice") {
        return InstanceRole::Voice;
    }
    return InstanceRole::Solo;
}

OutputMode parseOutputMode(std::string_view text) {
    return text == "none" ? OutputMode::None : OutputMode::OneVoice;
}

GenerationSettings sanitize(GenerationSettings settings) {
    if (!isValidPatternLength(settings.lengthBars)) {
        settings.lengthBars = 4;
    }
    settings.energyPct = std::clamp(settings.energyPct, 0, 100);
    settings.creativityPct = std::clamp(settings.creativityPct, 0, 100);
    return settings;
}

int clampOutputVoice(int voice) {
    return std::clamp(voice, 1, kMaxVoices);
}

} // namespace mm::core
