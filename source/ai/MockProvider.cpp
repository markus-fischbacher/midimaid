#include "ai/MockProvider.h"

#include <thread>

namespace mm::ai {

MockProvider::MockProvider(ProviderInfo info) : info_(std::move(info)) {}

void MockProvider::enqueue(AiResult result, std::chrono::milliseconds delay) {
    const std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back({std::move(result), delay});
}

void MockProvider::enqueueText(std::string text, std::chrono::milliseconds delay) {
    AiResult result;
    result.status = AiStatus::Ok;
    result.httpStatus = 200;
    result.text = std::move(text);
    enqueue(std::move(result), delay);
}

void MockProvider::enqueueError(AiStatus status, std::string message, int httpStatus) {
    AiResult result;
    result.status = status;
    result.message = std::move(message);
    result.httpStatus = httpStatus;
    enqueue(std::move(result));
}

void MockProvider::setConnectionStatus(ConnectionStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    connection_ = std::move(status);
}

std::vector<AiRequest> MockProvider::requests() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return requests_;
}

size_t MockProvider::requestCount() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return requests_.size();
}

AiResult MockProvider::generate(const AiRequest& request, const CancellationToken& token) {
    Script script;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        requests_.push_back(request);
        if (queue_.empty()) {
            AiResult none;
            none.status = AiStatus::NetworkError;
            none.message = "the mock provider has no answer queued";
            return none;
        }
        script = std::move(queue_.front());
        queue_.pop_front();
    }
    const auto until = std::chrono::steady_clock::now() + script.delay;
    while (std::chrono::steady_clock::now() < until) {
        if (token.cancelled()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (token.cancelled()) {
        AiResult cancelled;
        cancelled.status = AiStatus::Cancelled;
        return cancelled;
    }
    return script.result;
}

ConnectionStatus MockProvider::testConnection(const CancellationToken& token) {
    if (token.cancelled()) {
        return {false, AiStatus::Cancelled, {}, SchemaSupport::PromptOnly};
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    return connection_;
}

} // namespace mm::ai
