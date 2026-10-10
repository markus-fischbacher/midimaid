#pragma once

#include "ai/AiTypes.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mm::ai {

/// One HTTP request, as the providers need it (SPEC 7.1). The network layer behind `IHttpClient` is the only code
/// that touches the network: the providers are plain C++ and are tested with a scripted client and with a fake server.
struct HttpRequest {
    std::string method = "POST";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int connectTimeoutSeconds = 10; ///< the connection gets its own short timeout (SPEC 7.1)
    int timeoutSeconds = 30;        ///< for the whole request including the answer
};

enum class HttpError {
    None,
    Cancelled, ///< the token was cancelled
    Timeout,   ///< connection or answer took too long
    Connection ///< no connection: host not found, refused, reset ...
};

struct HttpResponse {
    HttpError error = HttpError::None;
    int status = 0; ///< HTTP status; 0 when there was none
    std::string body;
    std::string errorText;     ///< `error != None`: a readable cause
    int retryAfterSeconds = 0; ///< the `Retry-After` header when it was a number of seconds
};

/// Blocks until the answer is there, the timeout passed or the token was cancelled; a cancelled token ends the open
/// connection at once. Runs on a background thread of the caller, never on the message or audio thread.
class IHttpClient {
public:
    virtual ~IHttpClient() = default;
    virtual HttpResponse send(const HttpRequest& request, const CancellationToken& token) = 0;
};

struct RetryPolicy {
    int maxRetries = 2;     ///< after a 429 or a 5xx (SPEC 7.5); never after 401 or 400
    int baseDelayMs = 1000; ///< doubles with every retry
    int maxDelayMs = 10000; ///< also the cap for a `Retry-After` header
};

/// `http://` is only allowed towards this computer when a key is sent: a key must not travel in clear text over a
/// network. `https://` is always fine. Nullopt when the URL is acceptable, else the reason.
std::optional<std::string> checkUrl(const std::string& url, bool sendsKey);

/// Sends the request and maps the outcome to an `AiResult`: `text` is the response body on a 2xx answer. Retries
/// with backoff on 429 and 5xx (the waits end when the token is cancelled). Error messages are built from the error
/// text of the answer, cut short, without control characters, and `secret` is replaced by "***" wherever it occurs.
AiResult sendWithRetry(IHttpClient& client, const HttpRequest& request, const CancellationToken& token,
                       const RetryPolicy& retry, const std::string& secret);

/// The status for an HTTP answer: 401 and 403 are `AuthFailed`, 404 `ModelNotFound`, 429 `RateLimited`, 5xx
/// `ServerError`, other errors `BadRequest`.
AiStatus statusForHttp(int httpStatus);

/// Replaces every occurrence of `secret` (if not empty) in `text`.
std::string redact(std::string text, const std::string& secret);

} // namespace mm::ai
