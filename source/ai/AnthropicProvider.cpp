#include "ai/AnthropicProvider.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mm::ai {

namespace {

using nlohmann::json;

constexpr const char* kToolName = "submit_pattern";

AiResult failure(AiStatus status, std::string message) {
    AiResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

bool mentionsTemperature(const AiResult& result) {
    return result.status == AiStatus::BadRequest && result.message.find("temperature") != std::string::npos;
}

} // namespace

AnthropicProvider::AnthropicProvider(std::shared_ptr<IHttpClient> client, AnthropicConfig config)
    : client_(std::move(client)), config_(std::move(config)) {}

ProviderInfo AnthropicProvider::info() const {
    return {"anthropic", "Anthropic (Claude)", SchemaSupport::Enforced, config_.models};
}

HttpRequest AnthropicProvider::baseRequest(std::string method, const std::string& path) const {
    HttpRequest request;
    request.method = std::move(method);
    std::string base = config_.baseUrl;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    request.url = base + path;
    request.headers = {
        {"x-api-key", config_.apiKey}, {"anthropic-version", "2023-06-01"}, {"content-type", "application/json"}};
    request.connectTimeoutSeconds = config_.connectTimeoutSeconds;
    return request;
}

AiResult AnthropicProvider::generate(const AiRequest& request, const CancellationToken& token) {
    if (config_.apiKey.empty()) {
        return failure(AiStatus::AuthFailed, "no key is set");
    }
    const auto schema = json::parse(request.schemaJson, nullptr, false);
    if (schema.is_discarded() || !schema.is_object()) {
        return failure(AiStatus::BadRequest, "the response schema is not a JSON object");
    }
    json body = {{"model", request.model},
                 {"max_tokens", request.maxTokens},
                 {"system", request.systemPrompt},
                 {"messages", json::array({{{"role", "user"}, {"content", request.userPrompt}}})},
                 {"tools", json::array({{{"name", kToolName},
                                         {"description", "Submit the finished pattern."},
                                         {"input_schema", schema}}})},
                 {"tool_choice", {{"type", "tool"}, {"name", kToolName}}}};

    AiResult answer;
    for (const bool withTemperature : {true, false}) {
        if (withTemperature) {
            body["temperature"] = std::clamp(request.temperature, 0.0, 1.0);
        } else {
            body.erase("temperature"); // some models take no temperature: ask again without it
        }
        auto http = baseRequest("POST", "/v1/messages");
        http.body = body.dump();
        http.timeoutSeconds = request.timeoutSeconds;
        answer = sendWithRetry(*client_, http, token, config_.retry, config_.apiKey);
        if (answer.ok() || !(withTemperature && mentionsTemperature(answer))) {
            break;
        }
    }
    if (!answer.ok()) {
        return answer;
    }

    const auto parsed = json::parse(answer.text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return failure(AiStatus::BadRequest, "the answer of the provider is not JSON");
    }
    AiResult result;
    result.status = AiStatus::Ok;
    result.httpStatus = answer.httpStatus;
    if (const auto usage = parsed.find("usage"); usage != parsed.end() && usage->is_object()) {
        result.inputTokens = usage->value("input_tokens", 0);
        result.outputTokens = usage->value("output_tokens", 0);
    }
    // The tool input is the pattern; a plain text block is the fallback. No usable block: an empty text, which the
    // pipeline treats like any other unusable answer (one repair request).
    std::string fallback;
    if (const auto content = parsed.find("content"); content != parsed.end() && content->is_array()) {
        for (const auto& block : *content) {
            if (!block.is_object()) {
                continue;
            }
            const auto type = block.value("type", std::string());
            if (type == "tool_use" && block.contains("input")) {
                result.text = block["input"].dump();
                return result;
            }
            if (type == "text" && fallback.empty() && block.contains("text") && block["text"].is_string()) {
                fallback = block["text"].get<std::string>();
            }
        }
    }
    result.text = fallback;
    return result;
}

ConnectionStatus AnthropicProvider::testConnection(const CancellationToken& token) {
    ConnectionStatus status;
    if (config_.apiKey.empty()) {
        status.status = AiStatus::AuthFailed;
        status.message = "no key is set";
        return status;
    }
    auto http = baseRequest("GET", "/v1/models?limit=1");
    http.timeoutSeconds = 15;
    const auto answer = sendWithRetry(*client_, http, token, RetryPolicy{0, 0, 0}, config_.apiKey);
    status.ok = answer.ok();
    status.status = answer.status;
    status.message = answer.message;
    status.observed = SchemaSupport::Enforced; // the native API enforces the schema through the tool
    return status;
}

std::vector<std::string> AnthropicProvider::listModels(const CancellationToken& token) {
    std::vector<std::string> models;
    if (config_.apiKey.empty()) {
        return models;
    }
    auto http = baseRequest("GET", "/v1/models?limit=100");
    http.timeoutSeconds = 15;
    const auto answer = sendWithRetry(*client_, http, token, RetryPolicy{0, 0, 0}, config_.apiKey);
    if (!answer.ok()) {
        return models;
    }
    const auto parsed = json::parse(answer.text, nullptr, false);
    if (parsed.is_object() && parsed.contains("data") && parsed["data"].is_array()) {
        for (const auto& entry : parsed["data"]) {
            if (entry.is_object() && entry.contains("id") && entry["id"].is_string()) {
                models.push_back(entry["id"].get<std::string>());
            }
        }
    }
    return models;
}

} // namespace mm::ai
