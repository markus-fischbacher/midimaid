#include "core/GlobalSettings.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mm::core {

GlobalSettings sanitize(GlobalSettings settings) {
    const GlobalSettings defaults;
    if (settings.startRoot > 11) {
        settings.startRoot = defaults.startRoot;
    }
    if (findScale(settings.startScaleId) == nullptr) {
        settings.startScaleId = defaults.startScaleId;
    }
    settings.variationStrength = std::clamp(settings.variationStrength, 0, 100);
    if (!isValidDivision(settings.grid.division)) {
        settings.grid.division = defaults.grid.division;
    }
    return settings;
}

GlobalSettings parseGlobalSettings(std::string_view text) {
    using nlohmann::json;
    GlobalSettings settings;
    const json root = json::parse(text.begin(), text.end(), nullptr, false);
    if (!root.is_object()) {
        return settings;
    }
    const auto version = root.find("version");
    if (version == root.end() || !version->is_number_integer() || version->get<long long>() < 1) {
        return settings;
    }
    const auto boolean = [&](const char* key, bool& out) {
        if (const auto it = root.find(key); it != root.end() && it->is_boolean()) {
            out = it->get<bool>();
        }
    };
    const auto integer = [&](const char* key, long long low, long long high, auto& out) {
        if (const auto it = root.find(key); it != root.end() && it->is_number_integer()) {
            const auto value = it->get<long long>();
            if (value >= low && value <= high) {
                out = static_cast<std::remove_reference_t<decltype(out)>>(value);
            }
        }
    };
    boolean("expert", settings.expert);
    integer("startRoot", 0, 11, settings.startRoot);
    if (const auto it = root.find("startScale");
        it != root.end() && it->is_string() && findScale(it->get<std::string>()) != nullptr) {
        settings.startScaleId = it->get<std::string>();
    }
    integer("variationStrength", 0, 100, settings.variationStrength);
    if (const auto it = root.find("gridDivision"); it != root.end() && it->is_number_integer()) {
        const auto value = it->get<long long>();
        if (value > 0 && value < 1000 && isValidDivision(static_cast<uint32_t>(value))) {
            settings.grid.division = static_cast<uint32_t>(value);
        }
    }
    boolean("gridTriplet", settings.grid.triplet);
    boolean("snapChromatic", settings.snapChromatic);
    return settings;
}

std::string toJson(const GlobalSettings& input) {
    const auto settings = sanitize(input);
    nlohmann::json root;
    root["version"] = kGlobalSettingsVersion;
    root["expert"] = settings.expert;
    root["startRoot"] = settings.startRoot;
    root["startScale"] = settings.startScaleId;
    root["variationStrength"] = settings.variationStrength;
    root["gridDivision"] = settings.grid.division;
    root["gridTriplet"] = settings.grid.triplet;
    root["snapChromatic"] = settings.snapChromatic;
    return root.dump(2) + "\n";
}

} // namespace mm::core
