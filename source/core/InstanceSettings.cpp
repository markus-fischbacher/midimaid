#include "core/InstanceSettings.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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

double switchGridPpq(SwitchQuantize quantize) {
    switch (quantize) {
    case SwitchQuantize::Beat:
        return 1.0;
    case SwitchQuantize::Bars2:
        return 8.0;
    case SwitchQuantize::Bars4:
        return 16.0;
    case SwitchQuantize::Bars8:
        return 32.0;
    case SwitchQuantize::Bars16:
        return 64.0;
    case SwitchQuantize::Bar:
        break;
    }
    return 4.0;
}

SwitchQuantize switchQuantizeFromInt(int value) {
    return value >= 0 && value <= static_cast<int>(SwitchQuantize::Bars16) ? static_cast<SwitchQuantize>(value)
                                                                            : SwitchQuantize::Bar;
}

std::string formatVoiceRoles(const std::vector<VoiceRole>& roles) {
    std::string text;
    for (const VoiceRole role : roles) {
        if (!text.empty()) {
            text += ',';
        }
        text += toString(role);
    }
    return text;
}

std::vector<VoiceRole> parseVoiceRoles(std::string_view text) {
    std::vector<VoiceRole> roles;
    while (!text.empty() && roles.size() < static_cast<size_t>(kMaxVoices - 2)) {
        const size_t comma = text.find(',');
        if (const auto role = parseVoiceRole(text.substr(0, comma))) {
            roles.push_back(*role);
        }
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return roles;
}

GenerationSettings sanitize(GenerationSettings settings) {
    if (settings.extraVoices.size() > static_cast<size_t>(kMaxVoices - 2)) {
        settings.extraVoices.resize(static_cast<size_t>(kMaxVoices - 2));
    }
    if (!isValidPatternLength(settings.lengthBars)) {
        settings.lengthBars = 4;
    }
    settings.energyPct = std::clamp(settings.energyPct, 0, 100);
    settings.creativityPct = std::clamp(settings.creativityPct, 0, 100);
    settings.personalPct = std::clamp(settings.personalPct, 0, 100);
    if (settings.chromaticPct) {
        settings.chromaticPct = std::clamp(*settings.chromaticPct, 0, 100);
    }
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

int clampUiScale(int percent) {
    return std::clamp(percent, kMinUiScale, kMaxUiScale);
}

int uiScaleForDrag(int startPercent, double startWidth, double dragX) {
    if (!(startWidth > 0.0)) {
        return clampUiScale(startPercent);
    }
    const double scaled = startPercent * (startWidth + dragX) / startWidth;
    return clampUiScale(static_cast<int>(std::lround(std::clamp(scaled, -1.0e6, 1.0e6))));
}

int clampOutputVoice(int voice) {
    return std::clamp(voice, 1, kMaxVoices);
}

} // namespace mm::core
