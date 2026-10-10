#include "ai/OpenAiProvider.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace mm::ai {

namespace {

using nlohmann::json;

AiResult failure(AiStatus status, std::string message) {
    AiResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

bool mentionsTemperature(const AiResult& result) {
    return result.status == AiStatus::BadRequest && result.message.find("temperature") != std::string::npos;
}

/// The text of the first choice, or empty.
std::string firstContent(const json& answer) {
    if (!answer.is_object() || !answer.contains("choices") || !answer["choices"].is_array() ||
        answer["choices"].empty()) {
        return {};
    }
    const auto& choice = answer["choices"][0];
    if (!choice.is_object() || !choice.contains("message") || !choice["message"].is_object()) {
        return {};
    }
    const auto& message = choice["message"];
    if (message.contains("content") && message["content"].is_string() &&
        !message["content"].get<std::string>().empty()) {
        return message["content"].get<std::string>();
    }
    // Reasoning models behind LM Studio and others can put the whole answer, structured output included, into the
    // reasoning field and leave `content` empty. The parser decides whether it is a usable answer.
    for (const char* field : {"reasoning_content", "reasoning"}) {
        if (message.contains(field) && message[field].is_string() && !message[field].get<std::string>().empty()) {
            return message[field].get<std::string>();
        }
    }
    return {};
}

bool isChatModel(const std::string& id) {
    for (const char* skip : {"embedding", "whisper", "tts", "dall-e", "moderation", "audio", "realtime", "transcribe",
                             "image", "davinci", "babbage", "omni-moderation"}) {
        if (id.find(skip) != std::string::npos) {
            return false;
        }
    }
    return true;
}

} // namespace

OpenAiConfig openAiPreset(OpenAiPreset preset) {
    OpenAiConfig config;
    switch (preset) {
    case OpenAiPreset::OpenAi:
        config.id = "openai";
        config.name = "OpenAI (ChatGPT)";
        config.baseUrl = "https://api.openai.com/v1";
        config.schemaSupport = SchemaSupport::Enforced;
        config.useMaxCompletionTokens = true;
        config.filterChatModels = true;
        break;
    case OpenAiPreset::Ollama:
        config.id = "ollama";
        config.name = "Ollama (local)";
        config.baseUrl = "http://localhost:11434/v1";
        config.schemaSupport = SchemaSupport::Enforced; // from version 0.5 on, locally (SPEC 7.2)
        break;
    case OpenAiPreset::LmStudio:
        config.id = "lmstudio";
        config.name = "LM Studio (local)";
        config.baseUrl = "http://localhost:1234/v1";
        config.schemaSupport = SchemaSupport::Enforced;
        break;
    case OpenAiPreset::OpenRouter:
        config.id = "openrouter";
        config.name = "OpenRouter";
        config.baseUrl = "https://openrouter.ai/api/v1";
        config.schemaSupport = SchemaSupport::JsonOnly; // depends on the model behind it
        break;
    case OpenAiPreset::Custom:
        config.id = "custom";
        config.name = "OpenAI-compatible";
        config.schemaSupport = SchemaSupport::JsonOnly;
        break;
    }
    return config;
}

OpenAiProvider::OpenAiProvider(std::shared_ptr<IHttpClient> client, OpenAiConfig config)
    : client_(std::move(client)), config_(std::move(config)) {}

ProviderInfo OpenAiProvider::info() const {
    return {config_.id, config_.name, config_.schemaSupport, config_.models};
}

HttpRequest OpenAiProvider::baseRequest(std::string method, const std::string& path) const {
    HttpRequest request;
    request.method = std::move(method);
    std::string base = config_.baseUrl;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    request.url = base + path;
    request.headers = {{"content-type", "application/json"}};
    if (!config_.apiKey.empty()) {
        request.headers.emplace_back("authorization", "Bearer " + config_.apiKey);
    }
    request.connectTimeoutSeconds = config_.connectTimeoutSeconds;
    return request;
}

AiResult OpenAiProvider::chat(const std::string& model, const std::string& system, const std::string& user,
                              double temperature, int maxTokens, int timeoutSeconds, SchemaSupport level,
                              const std::string& schemaJson, const CancellationToken& token, const RetryPolicy& retry) {
    json body = {
        {"model", model},
        {"messages", json::array({{{"role", "system"}, {"content", system}}, {{"role", "user"}, {"content", user}}})}};
    body[config_.useMaxCompletionTokens ? "max_completion_tokens" : "max_tokens"] = maxTokens;
    if (level == SchemaSupport::Enforced) {
        const auto schema = json::parse(schemaJson, nullptr, false);
        if (schema.is_discarded() || !schema.is_object()) {
            return failure(AiStatus::BadRequest, "the response schema is not a JSON object");
        }
        // `strict` stays off: strict mode wants every property required, which the pattern schema does not do.
        body["response_format"] = {{"type", "json_schema"},
                                   {"json_schema", {{"name", "pattern"}, {"strict", false}, {"schema", schema}}}};
    } else if (level == SchemaSupport::JsonOnly) {
        body["response_format"] = {{"type", "json_object"}};
    }

    AiResult answer;
    for (const bool withTemperature : {true, false}) {
        if (withTemperature) {
            body["temperature"] = std::clamp(temperature, 0.0, 1.0) * config_.maxTemperature;
        } else {
            body.erase("temperature"); // reasoning models take none: ask again without it
        }
        auto http = baseRequest("POST", "/chat/completions");
        http.body = body.dump();
        http.timeoutSeconds = timeoutSeconds;
        answer = sendWithRetry(*client_, http, token, retry, config_.apiKey);
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
        result.inputTokens = usage->value("prompt_tokens", 0);
        result.outputTokens = usage->value("completion_tokens", 0);
    }
    result.text = firstContent(parsed);
    return result;
}

AiResult OpenAiProvider::generate(const AiRequest& request, const CancellationToken& token) {
    return chat(request.model, request.systemPrompt, request.userPrompt, request.temperature, request.maxTokens,
                request.timeoutSeconds, config_.schemaSupport, request.schemaJson, token, config_.retry);
}

ConnectionStatus OpenAiProvider::testConnection(const CancellationToken& token) {
    ConnectionStatus status;
    status.observed = config_.schemaSupport;
    if (config_.testModel.empty()) {
        status.status = AiStatus::ModelNotFound;
        status.message = "no model is chosen";
        return status;
    }
    // A schema that only one answer fits: a backend that enforces it must answer "cat" whatever it is asked.
    const std::string schema =
        R"({"type":"object","properties":{"answer":{"type":"string","enum":["cat"]}},"required":["answer"],"additionalProperties":false})";
    const auto answer = chat(config_.testModel, "Reply with one JSON object only.",
                             "Name any animal. Reply as JSON with the key \"answer\".", 0.0, 64, 30,
                             config_.schemaSupport, schema, token, RetryPolicy{0, 0, 0});
    status.ok = answer.ok();
    status.status = answer.status;
    status.message = answer.message;
    if (!answer.ok()) {
        return status;
    }
    // The test can only lower the level (SPEC 7.2): one good answer proves nothing about enforcement.
    const auto reply = json::parse(answer.text, nullptr, false);
    SchemaSupport measured = SchemaSupport::PromptOnly;
    if (reply.is_object()) {
        measured =
            reply.contains("answer") && reply["answer"] == "cat" ? SchemaSupport::Enforced : SchemaSupport::JsonOnly;
    }
    const auto rank = [](SchemaSupport level) { return static_cast<int>(level); }; // Enforced < JsonOnly < PromptOnly
    status.observed = rank(measured) > rank(config_.schemaSupport) ? measured : config_.schemaSupport;
    return status;
}

std::vector<std::string> OpenAiProvider::listModels(const CancellationToken& token) {
    std::vector<std::string> models;
    auto http = baseRequest("GET", "/models");
    http.timeoutSeconds = 15;
    const auto answer = sendWithRetry(*client_, http, token, RetryPolicy{0, 0, 0}, config_.apiKey);
    if (!answer.ok()) {
        return models;
    }
    const auto parsed = json::parse(answer.text, nullptr, false);
    if (parsed.is_object() && parsed.contains("data") && parsed["data"].is_array()) {
        for (const auto& entry : parsed["data"]) {
            if (entry.is_object() && entry.contains("id") && entry["id"].is_string()) {
                const auto id = entry["id"].get<std::string>();
                if (!config_.filterChatModels || isChatModel(id)) {
                    models.push_back(id);
                }
            }
        }
    }
    std::sort(models.begin(), models.end());
    return models;
}

} // namespace mm::ai
