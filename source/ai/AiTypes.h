#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace mm::ai {

/// How reliably a backend keeps to the response schema (SPEC 7.2). Every answer is validated whatever the level.
enum class SchemaSupport { Enforced, JsonOnly, PromptOnly };

struct ProviderInfo {
    std::string id;   ///< "anthropic", "openai", "ollama" ...
    std::string name; ///< for the UI
    SchemaSupport schemaSupport = SchemaSupport::PromptOnly;
    std::vector<std::string> models;
};

/// Cancels a running request (SPEC 7.1). Copies share the flag; `cancel` is safe from any thread. A provider polls it
/// between steps and hands it to the network layer, which ends the open connection.
class CancellationToken {
public:
    CancellationToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}
    void cancel() const { flag_->store(true); }
    bool cancelled() const { return flag_->load(); }
    /// The flag itself, for code that takes a plain `std::atomic<bool>` (the generators).
    const std::atomic<bool>* flag() const { return flag_.get(); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

struct AiRequest {
    std::string model;
    std::string systemPrompt;
    std::string userPrompt;
    std::string schemaJson;   ///< JSON schema of the answer; providers pass it on as far as they support it
    double temperature = 0.4; ///< 0 to 1, creativity-based; each provider maps it to its own range (SPEC 7.6)
    int maxTokens = 4096;
    int timeoutSeconds = 30;
};

enum class AiStatus {
    Ok,
    Cancelled,
    AuthFailed,    ///< 401, 403: the key is wrong
    ModelNotFound, ///< 404 or the provider says so
    RateLimited,   ///< 429, after the retries
    ServerError,   ///< 5xx, after the retries
    BadRequest,    ///< 400 and other client errors
    NetworkError,  ///< no connection, host not found, Ollama not running
    Timeout
};

struct AiResult {
    AiStatus status = AiStatus::NetworkError;
    std::string text;    ///< the answer (JSON text) on `Ok`
    int httpStatus = 0;  ///< 0 when there was none
    std::string message; ///< a readable cause for the error cases (never contains the key)
    int inputTokens = 0;
    int outputTokens = 0;

    bool ok() const { return status == AiStatus::Ok; }
};

struct ConnectionStatus {
    bool ok = false;
    AiStatus status = AiStatus::NetworkError;
    std::string message;
    /// The level the test observed; it can only lower the configured level, never raise it (SPEC 7.2).
    SchemaSupport observed = SchemaSupport::PromptOnly;
};

/// A provider (SPEC 7.1). Both calls block: they run as a job on a background thread of the caller (never on the
/// message or audio thread) and return when the answer is there, the token was cancelled or the timeout passed.
/// A cancelled request ends with `AiStatus::Cancelled` and its result is discarded.
class IAiProvider {
public:
    virtual ~IAiProvider() = default;
    virtual ProviderInfo info() const = 0;
    virtual AiResult generate(const AiRequest& request, const CancellationToken& token) = 0;
    virtual ConnectionStatus testConnection(const CancellationToken& token) = 0;
    /// The models the provider offers right now (for the model list of the settings), or an empty list when it cannot
    /// say. Blocks like `generate`. The default is the fixed list of `info()`.
    virtual std::vector<std::string> listModels(const CancellationToken&) { return info().models; }
};

} // namespace mm::ai
