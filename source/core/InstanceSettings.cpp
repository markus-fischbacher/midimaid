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

int clampOutputVoice(int voice) {
    return std::clamp(voice, 1, kMaxVoices);
}

} // namespace mm::core
