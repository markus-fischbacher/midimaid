#pragma once

#include "ai/AiTypes.h"
#include "ai/Http.h"

#include <memory>
#include <string>
#include <vector>

namespace mm::ai {

struct AnthropicConfig {
    std::string baseUrl = "https://api.anthropic.com";
    std::string apiKey; ///< from the OS keychain; never logged, never part of a message (SPEC 8.5)
    std::vector<std::string> models;
    RetryPolicy retry;
    int connectTimeoutSeconds = 10;
};

/// The native Anthropic Messages API (SPEC 7.2): the answer is forced through one tool whose input schema is the
/// response schema, so the model returns schema-shaped JSON (level `Enforced`). Blocks; runs on a background thread.
class AnthropicProvider : public IAiProvider {
public:
    AnthropicProvider(std::shared_ptr<IHttpClient> client, AnthropicConfig config);

    ProviderInfo info() const override;
    AiResult generate(const AiRequest& request, const CancellationToken& token) override;
    ConnectionStatus testConnection(const CancellationToken& token) override;
    std::vector<std::string> listModels(const CancellationToken& token) override;

private:
    HttpRequest baseRequest(std::string method, const std::string& path) const;

    std::shared_ptr<IHttpClient> client_;
    AnthropicConfig config_;
};

} // namespace mm::ai
