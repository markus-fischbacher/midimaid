#include "ai/Http.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <nlohmann/json.hpp>
#include <thread>

namespace mm::ai {

namespace {

using nlohmann::json;

std::string lowerHost(const std::string& url) {
    const auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        return {};
    }
    auto start = schemeEnd + 3;
    // Anything before an '@' is user info; the host is what follows.
    const auto pathStart = url.find_first_of("/?#", start);
    const auto at = url.find('@', start);
    if (at != std::string::npos && (pathStart == std::string::npos || at < pathStart)) {
        start = at + 1;
    }
    auto end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        authority = close == std::string::npos ? authority : authority.substr(0, close + 1);
    } else if (const auto colon = authority.find(':'); colon != std::string::npos) {
        authority = authority.substr(0, colon);
    }
    std::transform(authority.begin(), authority.end(), authority.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return authority;
}

bool startsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), text.begin());
}

/// Sleeps in short steps that look at the token. False when the token was cancelled.
bool waitFor(int milliseconds, const CancellationToken& token) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < end) {
        if (token.cancelled()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !token.cancelled();
}

std::string cleaned(std::string text, const std::string& secret) {
    text = redact(std::move(text), secret);
    for (auto& c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            c = ' ';
        }
    }
    constexpr size_t kMax = 300;
    if (text.size() > kMax) {
        size_t cut = kMax;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            --cut; // not in the middle of a UTF-8 character
        }
        text.resize(cut);
        text += "...";
    }
    return text;
}

/// The error text of a provider's answer: `{"error":{"message":..}}`, `{"error":"..."}` or `{"message":..}`.
std::string errorText(const std::string& body) {
    const auto parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }
    if (const auto error = parsed.find("error"); error != parsed.end()) {
        if (error->is_string()) {
            return error->get<std::string>();
        }
        if (error->is_object()) {
            if (const auto message = error->find("message"); message != error->end() && message->is_string()) {
                return message->get<std::string>();
            }
        }
    }
    if (const auto message = parsed.find("message"); message != parsed.end() && message->is_string()) {
        return message->get<std::string>();
    }
    return {};
}

AiResult failure(AiStatus status, int httpStatus, std::string message) {
    AiResult result;
    result.status = status;
    result.httpStatus = httpStatus;
    result.message = std::move(message);
    return result;
}

} // namespace

std::string redact(std::string text, const std::string& secret) {
    if (secret.empty()) {
        return text;
    }
    for (size_t at = text.find(secret); at != std::string::npos; at = text.find(secret, at + 3)) {
        text.replace(at, secret.size(), "***");
    }
    return text;
}

std::optional<std::string> checkUrl(const std::string& url, bool sendsKey) {
    const bool https = startsWith(url, "https://");
    const bool http = startsWith(url, "http://");
    if (!https && !http) {
        return std::string("the address must start with https:// or http://");
    }
    if (http && sendsKey) {
        const auto host = lowerHost(url);
        const bool loopback = host == "localhost" || host == "127.0.0.1" || host == "[::1]" ||
                              (host.size() > 10 && host.substr(host.size() - 10) == ".localhost");
        if (!loopback) {
            return std::string("a key is not sent over http to another computer: use https://");
        }
    }
    if (lowerHost(url).empty()) {
        return std::string("the address has no host");
    }
    return std::nullopt;
}

AiStatus statusForHttp(int httpStatus) {
    if (httpStatus == 401 || httpStatus == 403) {
        return AiStatus::AuthFailed;
    }
    if (httpStatus == 404) {
        return AiStatus::ModelNotFound;
    }
    if (httpStatus == 429) {
        return AiStatus::RateLimited;
    }
    if (httpStatus >= 500) {
        return AiStatus::ServerError;
    }
    return AiStatus::BadRequest;
}

AiResult sendWithRetry(IHttpClient& client, const HttpRequest& request, const CancellationToken& token,
                       const RetryPolicy& retry, const std::string& secret) {
    if (const auto problem = checkUrl(request.url, !secret.empty())) {
        return failure(AiStatus::BadRequest, 0, *problem);
    }
    for (int attempt = 0;; ++attempt) {
        if (token.cancelled()) {
            return failure(AiStatus::Cancelled, 0, {});
        }
        const HttpResponse response = client.send(request, token);
        if (response.error == HttpError::Cancelled || token.cancelled()) {
            return failure(AiStatus::Cancelled, 0, {});
        }
        if (response.error == HttpError::Timeout) {
            return failure(AiStatus::Timeout, 0, cleaned(response.errorText, secret));
        }
        if (response.error == HttpError::Connection) {
            return failure(AiStatus::NetworkError, 0, cleaned(response.errorText, secret));
        }
        if (response.status >= 200 && response.status < 300) {
            AiResult result;
            result.status = AiStatus::Ok;
            result.httpStatus = response.status;
            result.text = response.body;
            return result;
        }
        const bool retryable = response.status == 429 || response.status >= 500;
        if (retryable && attempt < retry.maxRetries) {
            const long long doubled = static_cast<long long>(retry.baseDelayMs) << std::min(attempt, 10);
            const long long hinted = response.retryAfterSeconds > 0 ? response.retryAfterSeconds * 1000LL : doubled;
            const int delay = static_cast<int>(std::min<long long>(hinted, retry.maxDelayMs));
            if (!waitFor(delay, token)) {
                return failure(AiStatus::Cancelled, 0, {});
            }
            continue;
        }
        auto text = errorText(response.body);
        if (text.empty()) {
            text = "HTTP " + std::to_string(response.status);
        }
        return failure(statusForHttp(response.status), response.status, cleaned(std::move(text), secret));
    }
}

} // namespace mm::ai
