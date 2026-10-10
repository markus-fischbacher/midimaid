#include "ai/ModelsConfig.h"

#include "ai/AnthropicProvider.h"
#include "ai/OpenAiProvider.h"

#include <nlohmann/json.hpp>

namespace mm::ai {

namespace {

using nlohmann::json;

std::optional<SchemaSupport> levelOf(const std::string& text) {
    if (text == "enforced") {
        return SchemaSupport::Enforced;
    }
    if (text == "json") {
        return SchemaSupport::JsonOnly;
    }
    if (text == "prompt") {
        return SchemaSupport::PromptOnly;
    }
    return std::nullopt;
}

} // namespace

const ProviderEntry* ModelsConfig::find(std::string_view id) const {
    for (const auto& entry : providers) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

ModelsConfig parseModelsConfig(std::string_view text) {
    ModelsConfig config;
    const auto root = json::parse(text.begin(), text.end(), nullptr, false);
    if (!root.is_object() || !root.contains("providers") || !root["providers"].is_array()) {
        return config;
    }
    for (const auto& node : root["providers"]) {
        if (!node.is_object()) {
            continue;
        }
        ProviderEntry entry;
        const auto text = [&](const char* key, std::string& out) {
            if (const auto it = node.find(key); it != node.end() && it->is_string()) {
                out = it->get<std::string>();
            }
        };
        const auto flag = [&](const char* key, bool& out) {
            if (const auto it = node.find(key); it != node.end() && it->is_boolean()) {
                out = it->get<bool>();
            }
        };
        text("id", entry.id);
        text("name", entry.name);
        text("baseUrl", entry.baseUrl);
        text("recommendedModel", entry.recommendedModel);
        flag("cloud", entry.cloud);
        flag("keyRequired", entry.keyRequired);
        flag("maxCompletionTokens", entry.maxCompletionTokens);
        flag("filterChatModels", entry.filterChatModels);
        std::string kind;
        text("kind", kind);
        if (entry.id.empty() || (kind != "anthropic" && kind != "openai")) {
            continue;
        }
        entry.kind = kind == "anthropic" ? ProviderKind::Anthropic : ProviderKind::OpenAi;
        if (entry.name.empty()) {
            entry.name = entry.id;
        }
        std::string level;
        text("schemaSupport", level);
        if (const auto parsed = levelOf(level)) {
            entry.schemaSupport = *parsed;
        }
        if (const auto it = node.find("timeoutSeconds"); it != node.end() && it->is_number_integer()) {
            const auto value = it->get<long long>();
            if (value >= 5 && value <= 600) {
                entry.timeoutSeconds = static_cast<int>(value);
            }
        }
        if (const auto it = node.find("models"); it != node.end() && it->is_array()) {
            for (const auto& model : *it) {
                if (model.is_string() && !model.get<std::string>().empty()) {
                    entry.models.push_back(model.get<std::string>());
                }
            }
        }
        if (!entry.baseUrl.empty() && isLocalUrl(entry.baseUrl)) {
            entry.cloud = false;
        }
        bool duplicate = false;
        for (const auto& known : config.providers) {
            duplicate = duplicate || known.id == entry.id;
        }
        if (!duplicate) {
            config.providers.push_back(std::move(entry));
        }
    }
    return config;
}

ModelsConfig mergeModelsConfig(ModelsConfig shipped, const ModelsConfig& user) {
    for (const auto& entry : user.providers) {
        bool replaced = false;
        for (auto& known : shipped.providers) {
            if (known.id == entry.id) {
                known = entry;
                replaced = true;
            }
        }
        if (!replaced) {
            shipped.providers.push_back(entry);
        }
    }
    return shipped;
}

std::string effectiveModel(const ProviderEntry& entry, const ProviderChoice& choice) {
    return choice.model.empty() ? entry.recommendedModel : choice.model;
}

int effectiveTimeout(const ProviderEntry& entry, const ProviderChoice& choice) {
    return choice.timeoutSeconds > 0 ? choice.timeoutSeconds : entry.timeoutSeconds;
}

int effectiveMaxTokens(const ProviderChoice& choice) {
    return choice.maxTokens > 0 ? choice.maxTokens : 4096;
}

std::string effectiveBaseUrl(const ProviderEntry& entry, const ProviderChoice& choice) {
    return choice.baseUrl.empty() ? entry.baseUrl : choice.baseUrl;
}

std::shared_ptr<IAiProvider> makeProvider(const ProviderEntry& entry, const ProviderChoice& choice, std::string apiKey,
                                          std::shared_ptr<IHttpClient> client) {
    const auto baseUrl = effectiveBaseUrl(entry, choice);
    const auto model = effectiveModel(entry, choice);
    if (entry.kind == ProviderKind::Anthropic) {
        AnthropicConfig config;
        config.baseUrl = baseUrl;
        config.apiKey = std::move(apiKey);
        config.models = entry.models;
        return std::make_shared<AnthropicProvider>(std::move(client), std::move(config));
    }
    OpenAiConfig config;
    config.id = entry.id;
    config.name = entry.name;
    config.baseUrl = baseUrl;
    config.apiKey = std::move(apiKey);
    config.models = entry.models;
    config.testModel = model;
    config.schemaSupport = levelOf(choice.schemaLevel).value_or(entry.schemaSupport);
    config.useMaxCompletionTokens = entry.maxCompletionTokens;
    config.filterChatModels = entry.filterChatModels;
    return std::make_shared<OpenAiProvider>(std::move(client), std::move(config));
}

} // namespace mm::ai
