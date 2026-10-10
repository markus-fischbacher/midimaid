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

    // JUCE's connection timeout is, on macOS, the idle timeout of the whole request: the time until the first byte
    // of the answer. A model that thinks for a minute must not run into a 10 second "connect" timeout, so the
    // timeout of the whole request is used (a refused connection fails at once anyway). `connectTimeoutSeconds` of
    // the request cannot be kept apart here; the watcher below enforces the total time.
    const int timeoutSeconds = std::max(request.timeoutSeconds, 1);
    juce::WebInputStream stream(url, post);
    stream.withExtraHeaders(headers).withConnectionTimeout(timeoutSeconds * 1000).withNumRedirectsToFollow(0);
    const auto started = std::chrono::steady_clock::now();
    Watcher watcher(stream, token, timeoutSeconds);

    stream.connect(nullptr);
    response.status = stream.getStatusCode();
    if (response.status == 0) { // no answer at all (JUCE may still report the connect as done)
        response.error = watcher.reason() != HttpError::None ? watcher.reason() : HttpError::Connection;
        if (response.error == HttpError::Connection &&
            std::chrono::steady_clock::now() - started >=
                std::chrono::seconds(timeoutSeconds) - std::chrono::milliseconds(500)) {
            response.error = HttpError::Timeout; // the system gave up waiting (its own timeout), it is not a refusal
        }
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
    // JUCE hides the error of a transfer that the system ended early (it reports it as finished), so the length the
    // server announced is the only measure. A chunked answer has no length: a cut-off one is caught by the parser
    // (a JSON text that ends early is an unusable answer, D-172).
    const auto announced = stream.getTotalLength();
    const bool incomplete = announced >= 0 && static_cast<juce::int64>(response.body.size()) < announced &&
                            response.body.size() < kMaxBodyBytes;
    if (watcher.reason() != HttpError::None) {
        response.error = watcher.reason(); // ended while the answer was coming in: it is incomplete
    } else if (incomplete) {
        // The system ended the transfer itself (its idle timeout can be a moment earlier than ours).
        const bool late = std::chrono::steady_clock::now() - started >=
                          std::chrono::seconds(timeoutSeconds) - std::chrono::milliseconds(500);
        response.error = late ? HttpError::Timeout : HttpError::Connection;
    }
    if (response.error == HttpError::Timeout) {
        response.errorText = "the answer took too long";
    } else if (response.error == HttpError::Connection) {
        response.errorText = "the answer was cut off";
    }
    return response;
}

} // namespace mm::plugin
