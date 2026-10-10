#include "plugin/JuceHttpClient.h"

#include <atomic>
#include <chrono>
#include <juce_core/juce_core.h>
#include <thread>

namespace mm::plugin {

namespace {

constexpr size_t kMaxBodyBytes = 8 * 1024 * 1024; // an answer larger than this is cut (the parser rejects it anyway)

/// Ends the stream when the token is cancelled or the deadline passes, and remembers which of the two it was.
class Watcher {
public:
    Watcher(juce::WebInputStream& stream, const mm::ai::CancellationToken& token, int timeoutSeconds)
        : stream_(stream), token_(token),
          deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(std::max(1, timeoutSeconds))),
          thread_([this] { run(); }) {}

    ~Watcher() {
        done_.store(true);
        thread_.join();
    }

    mm::ai::HttpError reason() const { return static_cast<mm::ai::HttpError>(reason_.load()); }

private:
    void run() {
        while (!done_.load()) {
            if (token_.cancelled()) {
                reason_.store(static_cast<int>(mm::ai::HttpError::Cancelled));
                stream_.cancel();
                return;
            }
            if (std::chrono::steady_clock::now() > deadline_) {
                reason_.store(static_cast<int>(mm::ai::HttpError::Timeout));
                stream_.cancel();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    juce::WebInputStream& stream_;
    const mm::ai::CancellationToken& token_;
    std::chrono::steady_clock::time_point deadline_;
    std::atomic<bool> done_{false};
    std::atomic<int> reason_{static_cast<int>(mm::ai::HttpError::None)};
    std::thread thread_; // last: starts when everything above exists
};

} // namespace

mm::ai::HttpResponse JuceHttpClient::send(const mm::ai::HttpRequest& request, const mm::ai::CancellationToken& token) {
    using mm::ai::HttpError;
    mm::ai::HttpResponse response;
    if (token.cancelled()) {
        response.error = HttpError::Cancelled;
        return response;
    }
    const bool post = request.method == "POST";
    juce::URL url{juce::String(request.url)};
    if (post) {
        url = url.withPOSTData(juce::String::fromUTF8(request.body.c_str()));
    }
    juce::String headers;
    for (const auto& [name, value] : request.headers) {
        headers << juce::String(name) << ": " << juce::String(value) << "\r\n";
    }

    juce::WebInputStream stream(url, post);
    stream.withExtraHeaders(headers)
        .withConnectionTimeout(request.connectTimeoutSeconds * 1000)
        .withNumRedirectsToFollow(0);
    Watcher watcher(stream, token, request.timeoutSeconds);

    const bool connected = stream.connect(nullptr);
    response.status = stream.getStatusCode();
    if (!connected && response.status == 0) {
        response.error = watcher.reason() != HttpError::None ? watcher.reason() : HttpError::Connection;
        if (response.error == HttpError::Connection) {
            response.errorText = "no connection";
        } else if (response.error == HttpError::Timeout) {
            response.errorText = "the connection timed out";
        }
        return response;
    }
    const auto retryAfter = stream.getResponseHeaders()["Retry-After"];
    if (retryAfter.isNotEmpty() && retryAfter.containsOnly("0123456789")) {
        response.retryAfterSeconds = std::min(retryAfter.getIntValue(), 3600);
    }
    char buffer[8192];
    while (response.body.size() < kMaxBodyBytes) {
        const int count = stream.read(buffer, static_cast<int>(sizeof(buffer)));
        if (count <= 0) {
            break;
        }
        response.body.append(buffer, static_cast<size_t>(count));
    }
    if (watcher.reason() != HttpError::None) {
        response.error = watcher.reason(); // ended while the answer was coming in: it is incomplete
        response.errorText = response.error == HttpError::Timeout ? "the answer took too long" : "";
    }
    return response;
}

} // namespace mm::plugin
