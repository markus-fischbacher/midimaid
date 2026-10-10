#include "core/GlobalSettings.h"

#include "core/DrumReference.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace mm::core {

namespace {

constexpr size_t kMaxIdBytes = 64;
constexpr size_t kMaxModelBytes = 200;
constexpr size_t kMaxUrlBytes = 500;
constexpr size_t kMaxProviders = 16;

bool validId(const std::string& id) {
    return !id.empty() && id.size() <= kMaxIdBytes &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}

/// Cuts at a UTF-8 character boundary.
std::string cut(std::string text, size_t limit) {
    if (text.size() > limit) {
        size_t at = limit;
        while (at > 0 && (static_cast<unsigned char>(text[at]) & 0xC0) == 0x80) {
            --at;
        }
        text.resize(at);
    }
    return text;
}

AiProviderSettings sanitize(AiProviderSettings settings) {
    settings.model = cut(std::move(settings.model), kMaxModelBytes);
    settings.baseUrl = cut(std::move(settings.baseUrl), kMaxUrlBytes);
    if (settings.timeoutSeconds != 0) {
        settings.timeoutSeconds = std::clamp(settings.timeoutSeconds, 5, 600);
    }
    if (settings.maxTokens != 0) {
        settings.maxTokens = std::clamp(settings.maxTokens, 256, 64000);
    }
    if (settings.schemaLevel != "enforced" && settings.schemaLevel != "json" && settings.schemaLevel != "prompt") {
        settings.schemaLevel.clear();
    }
    return settings;
}

AiSettings sanitize(AiSettings settings) {
    if (!settings.activeProvider.empty() && !validId(settings.activeProvider)) {
        settings.activeProvider.clear();
    }
    std::map<std::string, AiProviderSettings> providers;
    for (auto& [id, value] : settings.providers) {
        if (validId(id) && providers.size() < kMaxProviders) {
            providers[id] = sanitize(std::move(value));
        }
    }
    settings.providers = std::move(providers);
    std::set<std::string> consent;
    for (const auto& id : settings.cloudConsent) {
        if (validId(id) && consent.size() < kMaxProviders) {
            consent.insert(id);
        }
    }
    settings.cloudConsent = std::move(consent);
    return settings;
}

AiSettings parseAi(const nlohmann::json& node) {
    using nlohmann::json;
    AiSettings ai;
    if (!node.is_object()) {
        return ai;
    }
    if (const auto it = node.find("activeProvider"); it != node.end() && it->is_string()) {
        ai.activeProvider = it->get<std::string>();
    }
    if (const auto it = node.find("logPrompts"); it != node.end() && it->is_boolean()) {
        ai.logPrompts = it->get<bool>();
    }
    if (const auto it = node.find("cloudConsent"); it != node.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (entry.is_string()) {
                ai.cloudConsent.insert(entry.get<std::string>());
            }
        }
    }
    if (const auto it = node.find("providers"); it != node.end() && it->is_object()) {
        for (const auto& [id, value] : it->items()) {
            if (!value.is_object()) {
                continue;
            }
            AiProviderSettings provider;
            const auto text = [&](const char* key, std::string& out) {
                if (const auto field = value.find(key); field != value.end() && field->is_string()) {
                    out = field->get<std::string>();
                }
            };
            const auto number = [&](const char* key, int& out) {
                if (const auto field = value.find(key); field != value.end() && field->is_number_integer()) {
                    const auto n = field->get<long long>();
                    if (n >= 0 && n <= 1000000) {
                        out = static_cast<int>(n);
                    }
                }
            };
            text("model", provider.model);
            text("baseUrl", provider.baseUrl);
            text("schemaLevel", provider.schemaLevel);
            number("timeoutSeconds", provider.timeoutSeconds);
            number("maxTokens", provider.maxTokens);
            ai.providers[id] = provider;
        }
    }
    return ai;
}

nlohmann::json aiToJson(const AiSettings& ai) {
    using nlohmann::json;
    json node;
    node["activeProvider"] = ai.activeProvider;
    node["logPrompts"] = ai.logPrompts;
    node["cloudConsent"] = json::array();
    for (const auto& id : ai.cloudConsent) {
        node["cloudConsent"].push_back(id);
    }
    node["providers"] = json::object();
    for (const auto& [id, provider] : ai.providers) {
        node["providers"][id] = {{"model", provider.model},
                                 {"baseUrl", provider.baseUrl},
                                 {"timeoutSeconds", provider.timeoutSeconds},
                                 {"maxTokens", provider.maxTokens},
                                 {"schemaLevel", provider.schemaLevel}};
    }
    return node;
}

} // namespace

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
    settings.drumMap = drumMappingToText(drumMappingFromText(settings.drumMap)); // only what can be read stays
    settings.ai = sanitize(std::move(settings.ai));
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
    if (const auto it = root.find("drumMap"); it != root.end() && it->is_string()) {
        settings.drumMap = it->get<std::string>();
    }
    if (const auto it = root.find("ai"); it != root.end()) {
        settings.ai = parseAi(*it);
    }
    return sanitize(std::move(settings));
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
    root["drumMap"] = settings.drumMap;
    root["ai"] = aiToJson(settings.ai);
    return root.dump(2) + "\n";
}

} // namespace mm::core
