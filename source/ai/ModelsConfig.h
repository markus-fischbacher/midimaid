#pragma once

#include "ai/AiTypes.h"
#include "ai/Http.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::ai {

enum class ProviderKind { Anthropic, OpenAi };

/// One provider of `models.json` (SPEC 7.2, 7.7): address, level of schema support, timeout and the recommended
/// models. Names of models are data, not code.
struct ProviderEntry {
    std::string id;
    std::string name;
    ProviderKind kind = ProviderKind::OpenAi;
    std::string baseUrl;
    bool cloud = true; ///< false for a provider on this computer (no consent needed for references, SPEC 3.20)
    bool keyRequired = true;
    SchemaSupport schemaSupport = SchemaSupport::JsonOnly;
    int timeoutSeconds = 30;
    bool maxCompletionTokens = false;
    bool filterChatModels = false;
    std::string recommendedModel; ///< empty: the model comes from the provider's list
    std::vector<std::string> models;
};

struct ModelsConfig {
    std::vector<ProviderEntry> providers;

    const ProviderEntry* find(std::string_view id) const;
};

/// Reads `models.json`. Never fails: unreadable text gives an empty list; a bad entry (no id, unknown kind) is left
/// out, a bad field gets its default. A provider whose address is local (`localhost`, `127.0.0.1`, `[::1]`) counts as
/// not cloud whatever the file says.
ModelsConfig parseModelsConfig(std::string_view text);

/// The shipped list with the entries of the user's file on top: an entry with the id of a shipped one replaces it,
/// others are added (SPEC 7.7: overridable in the data folder).
ModelsConfig mergeModelsConfig(ModelsConfig shipped, const ModelsConfig& user);

/// What the musician chose, resolved against the entry (see `AiProviderSettings`).
struct ProviderChoice {
    std::string model;       ///< empty: the recommended model of the entry
    std::string baseUrl;     ///< empty: the entry's
    int timeoutSeconds = 0;  ///< 0: the entry's
    int maxTokens = 0;       ///< 0: 4096
    std::string schemaLevel; ///< "", "enforced", "json" or "prompt"
};

/// The model the request uses: the choice, else the recommended one.
std::string effectiveModel(const ProviderEntry& entry, const ProviderChoice& choice);
int effectiveTimeout(const ProviderEntry& entry, const ProviderChoice& choice);
int effectiveMaxTokens(const ProviderChoice& choice);
std::string effectiveBaseUrl(const ProviderEntry& entry, const ProviderChoice& choice);

/// Builds the provider for an entry (SPEC 7.2). `apiKey` may be empty. Never sends anything by itself.
std::shared_ptr<IAiProvider> makeProvider(const ProviderEntry& entry, const ProviderChoice& choice, std::string apiKey,
                                          std::shared_ptr<IHttpClient> client);

} // namespace mm::ai
