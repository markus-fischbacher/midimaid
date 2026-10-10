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
    if (settings.root && *settings.root > 11) {
        settings.root.reset();
    }
    if (settings.scaleId && findScale(*settings.scaleId) == nullptr) {
        settings.scaleId.reset();
    }
    if (settings.prompt.size() > kMaxPromptBytes) {
        size_t cut = kMaxPromptBytes;
        while (cut > 0 && (static_cast<unsigned char>(settings.prompt[cut]) & 0xC0) == 0x80) {
            --cut; // not in the middle of a UTF-8 character
        }
        settings.prompt.resize(cut);
    }
    return settings;
}

std::optional<uint64_t> parseSeed(std::string_view text) {
    if (text.empty() || text.size() > 20) {
        return std::nullopt;
    }
    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const auto digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) {
            return std::nullopt; // does not fit
        }
        value = value * 10 + digit;
    }
    return value;
}

int clampOctave(int octave) {
    return std::clamp(octave, -2, 2);
}

int clampOutputVoice(int voice) {
    return std::clamp(voice, 1, kMaxVoices);
}

} // namespace mm::core
