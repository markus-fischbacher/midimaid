#pragma once

#include "ai/AiTypes.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

namespace mm::ai {

/// A provider for the tests (and for development without network): it answers with what was queued, in order, and
/// records the requests. An empty queue gives a network error. A queued delay is waited out in short steps that look
/// at the token, so a cancelled request ends at once with `Cancelled`.
class MockProvider : public IAiProvider {
public:
    explicit MockProvider(ProviderInfo info = {"mock", "Mock", SchemaSupport::Enforced, {"mock-model"}});

    /// Queues an answer; `delay` is how long the provider "works" before it answers.
    void enqueue(AiResult result, std::chrono::milliseconds delay = std::chrono::milliseconds(0));
    /// Queues a successful answer with `text`.
    void enqueueText(std::string text, std::chrono::milliseconds delay = std::chrono::milliseconds(0));
    /// Queues an error.
    void enqueueError(AiStatus status, std::string message = {}, int httpStatus = 0);
    void setConnectionStatus(ConnectionStatus status);

    /// The requests received so far, oldest first.
    std::vector<AiRequest> requests() const;
    size_t requestCount() const;

    ProviderInfo info() const override { return info_; }
    AiResult generate(const AiRequest& request, const CancellationToken& token) override;
    ConnectionStatus testConnection(const CancellationToken& token) override;

private:
    struct Script {
        AiResult result;
        std::chrono::milliseconds delay;
    };

    ProviderInfo info_;
    mutable std::mutex mutex_;
    std::deque<Script> queue_;
    std::vector<AiRequest> requests_;
    ConnectionStatus connection_{true, AiStatus::Ok, {}, SchemaSupport::Enforced};
};

} // namespace mm::ai
