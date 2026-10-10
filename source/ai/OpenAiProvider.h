#pragma once

#include "ai/AiTypes.h"
#include "ai/Http.h"

#include <memory>
#include <string>
#include <vector>

namespace mm::ai {

struct OpenAiConfig {
    std::string id = "openai-compatible";
    std::string name = "OpenAI-compatible";
    std::string baseUrl;             ///< up to and including the version, e.g. "https://api.openai.com/v1"
    std::string apiKey;              ///< may be empty (Ollama, LM Studio); from the OS keychain, never logged
    std::vector<std::string> models; ///< fixed list; the live list comes from `listModels`
    std::string testModel;           ///< the model of the connection test (the one the musician chose)
    /// How well the backend keeps to the schema (SPEC 7.2). The connection test can only lower it.
    SchemaSupport schemaSupport = SchemaSupport::JsonOnly;
    bool useMaxCompletionTokens = false; ///< OpenAI: `max_completion_tokens` instead of `max_tokens`
    double maxTemperature = 1.2;         ///< creativity 100 % maps to this temperature (SPEC 7.6)
    bool filterChatModels = false;       ///< OpenAI: leave embeddings, speech and image models out of the list
    RetryPolicy retry;
    int connectTimeoutSeconds = 10;
};

/// One provider for every backend with an OpenAI-style `/chat/completions` (OpenAI, Ollama, LM Studio, OpenRouter,
/// own address; SPEC 7.2). The response format follows the level: JSON schema, JSON object or only the prompt.
class OpenAiProvider : public IAiProvider {
public:
    OpenAiProvider(std::shared_ptr<IHttpClient> client, OpenAiConfig config);

    ProviderInfo info() const override;
    AiResult generate(const AiRequest& request, const CancellationToken& token) override;
    ConnectionStatus testConnection(const CancellationToken& token) override;
    std::vector<std::string> listModels(const CancellationToken& token) override;

private:
    HttpRequest baseRequest(std::string method, const std::string& path) const;
    AiResult chat(const std::string& model, const std::string& system, const std::string& user, double temperature,
                  int maxTokens, int timeoutSeconds, SchemaSupport level, const std::string& schemaJson,
                  const CancellationToken& token, const RetryPolicy& retry);

    std::shared_ptr<IHttpClient> client_;
    OpenAiConfig config_;
};

/// The presets of SPEC 7.2. `levelSource`: the level is kept here for now and moves to `models.json` with the
/// settings dialog (SPEC 7.2: the file is authoritative).
enum class OpenAiPreset { OpenAi, Ollama, LmStudio, OpenRouter, Custom };

/// The configuration of a preset without key and models; `Custom` is limited to `JsonOnly` (SPEC 7.2).
OpenAiConfig openAiPreset(OpenAiPreset preset);

} // namespace mm::ai
