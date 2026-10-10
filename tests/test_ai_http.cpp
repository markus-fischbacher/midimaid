#include "ai/AiGeneration.h"
#include "ai/AnthropicProvider.h"
#include "ai/Http.h"
#include "ai/OpenAiProvider.h"
#include "ai/PatternCompact.h"
#include "core/PatternGenerator.h"
#include "core/StyleProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <deque>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

using namespace mm::ai;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

/// Answers with what was queued, in order, and records the requests. An empty queue is a connection error.
class ScriptedClient : public IHttpClient {
public:
    struct Step {
        HttpResponse response;
        std::chrono::milliseconds delay{0};
    };

    void push(HttpResponse response, std::chrono::milliseconds delay = 0ms) {
        const std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({std::move(response), delay});
    }
    void pushStatus(int status, std::string body = {}, int retryAfter = 0) {
        HttpResponse response;
        response.status = status;
        response.body = std::move(body);
        response.retryAfterSeconds = retryAfter;
        push(std::move(response));
    }
    void pushOk(std::string body) { pushStatus(200, std::move(body)); }
    void pushError(HttpError error, std::string text = "x") {
        HttpResponse response;
        response.error = error;
        response.errorText = std::move(text);
        push(std::move(response));
    }

    HttpResponse send(const HttpRequest& request, const CancellationToken& token) override {
        Step step;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            requests_.push_back(request);
            if (queue_.empty()) {
                HttpResponse refused;
                refused.error = HttpError::Connection;
                refused.errorText = "queue empty";
                return refused;
            }
            step = std::move(queue_.front());
            queue_.pop_front();
        }
        const auto end = std::chrono::steady_clock::now() + step.delay;
        while (std::chrono::steady_clock::now() < end) {
            if (token.cancelled()) {
                HttpResponse cancelled;
                cancelled.error = HttpError::Cancelled;
                return cancelled;
            }
            std::this_thread::sleep_for(5ms);
        }
        return step.response;
    }

    std::vector<HttpRequest> requests() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        return requests_;
    }
    size_t count() const { return requests().size(); }

private:
    mutable std::mutex mutex_;
    std::deque<Step> queue_;
    std::vector<HttpRequest> requests_;
};

constexpr RetryPolicy kFast{2, 1, 5};

std::string header(const HttpRequest& request, const std::string& name) {
    for (const auto& [key, value] : request.headers) {
        if (key == name) {
            return value;
        }
    }
    return {};
}

AiRequest sampleRequest() {
    AiRequest request;
    request.model = "some-model";
    request.systemPrompt = "SYSTEM TEXT";
    request.userPrompt = "USER TEXT";
    request.schemaJson = schemaV1Json();
    request.temperature = 0.5;
    request.maxTokens = 1234;
    request.timeoutSeconds = 7;
    return request;
}

std::string anthropicAnswer(const std::string& patternJson, int in = 11, int out = 22) {
    return json{
        {"content",
         json::array({{{"type", "tool_use"}, {"name", "submit_pattern"}, {"input", json::parse(patternJson)}}})},
        {"usage", {{"input_tokens", in}, {"output_tokens", out}}}}
        .dump();
}

std::string openAiAnswer(const std::string& content, int in = 5, int out = 6) {
    return json{{"choices", json::array({{{"message", {{"role", "assistant"}, {"content", content}}}}})},
                {"usage", {{"prompt_tokens", in}, {"completion_tokens", out}}}}
        .dump();
}

mm::core::StyleProfile style() {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/peak_time.json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = mm::core::loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

std::string patternAnswer() {
    const auto profile = style();
    mm::core::GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 3;
    const auto result = mm::core::generatePattern(profile, request);
    REQUIRE(result.success);
    return patternToSchemaJson(result.pattern, &profile, false)->json;
}

AnthropicConfig anthropicConfig() {
    AnthropicConfig config;
    config.apiKey = "sk-ant-SECRET";
    config.retry = kFast;
    return config;
}

OpenAiConfig openAiConfig(SchemaSupport level, const std::string& key = "sk-SECRET") {
    OpenAiConfig config = openAiPreset(OpenAiPreset::OpenAi);
    config.apiKey = key;
    config.schemaSupport = level;
    config.retry = kFast;
    config.testModel = "gpt-test";
    return config;
}

} // namespace

TEST_CASE("urls: https always, http only to this computer when a key is sent", "[ai-http]") {
    CHECK_FALSE(checkUrl("https://api.example.com/v1", true));
    CHECK_FALSE(checkUrl("http://localhost:11434/v1", true));
    CHECK_FALSE(checkUrl("http://127.0.0.1:8080/x", true));
    CHECK_FALSE(checkUrl("http://[::1]:8080/x", true));
    CHECK_FALSE(checkUrl("http://foo.localhost/x", true));
    CHECK_FALSE(checkUrl("http://192.168.1.5:1234/v1", false)); // no key: nothing to protect
    CHECK(checkUrl("http://192.168.1.5:1234/v1", true));
    CHECK(checkUrl("http://api.example.com/v1", true));
    CHECK(checkUrl("http://localhost.evil.com/v1", true));
    CHECK(checkUrl("http://localhost@evil.com/v1", true));       // user info is not the host
    CHECK_FALSE(checkUrl("http://user:pw@localhost:1/x", true)); // the host is what follows the user info
    CHECK(checkUrl("http://evil/x", true));
    CHECK(checkUrl("ftp://localhost/x", false));
    CHECK(checkUrl("localhost:11434", false));
    CHECK(checkUrl("https://", false));
}

TEST_CASE("http statuses map to provider statuses", "[ai-http]") {
    CHECK(statusForHttp(401) == AiStatus::AuthFailed);
    CHECK(statusForHttp(403) == AiStatus::AuthFailed);
    CHECK(statusForHttp(404) == AiStatus::ModelNotFound);
    CHECK(statusForHttp(429) == AiStatus::RateLimited);
    CHECK(statusForHttp(500) == AiStatus::ServerError);
    CHECK(statusForHttp(529) == AiStatus::ServerError);
    CHECK(statusForHttp(400) == AiStatus::BadRequest);
    CHECK(statusForHttp(422) == AiStatus::BadRequest);
}

TEST_CASE("redact replaces every occurrence of the secret", "[ai-http]") {
    CHECK(redact("a KEY b KEY", "KEY") == "a *** b ***");
    CHECK(redact("nothing", "KEY") == "nothing");
    CHECK(redact("KEY", "") == "KEY");
}

TEST_CASE("sendWithRetry retries 429 and 5xx twice with backoff and not 400 or 401", "[ai-http]") {
    HttpRequest request;
    request.url = "https://x.test/a";
    const CancellationToken token;
    SECTION("two failures, then success") {
        ScriptedClient client;
        client.pushStatus(429);
        client.pushStatus(503);
        client.pushOk("fine");
        const auto result = sendWithRetry(client, request, token, kFast, "");
        CHECK(result.ok());
        CHECK(result.text == "fine");
        CHECK(client.count() == 3);
    }
    SECTION("three failures give up with the last status") {
        ScriptedClient client;
        for (int i = 0; i < 3; ++i) {
            client.pushStatus(429);
        }
        client.pushOk("never");
        const auto result = sendWithRetry(client, request, token, kFast, "");
        CHECK(result.status == AiStatus::RateLimited);
        CHECK(result.httpStatus == 429);
        CHECK(client.count() == 3);
    }
    SECTION("500 gives ServerError after the retries") {
        ScriptedClient client;
        for (int i = 0; i < 3; ++i) {
            client.pushStatus(500);
        }
        CHECK(sendWithRetry(client, request, token, kFast, "").status == AiStatus::ServerError);
        CHECK(client.count() == 3);
    }
    SECTION("401 and 400 are not repeated") {
        for (const int status : {401, 400, 404}) {
            ScriptedClient client;
            client.pushStatus(status);
            client.pushOk("never");
            const auto result = sendWithRetry(client, request, token, kFast, "");
            CHECK(result.httpStatus == status);
            CHECK(client.count() == 1);
        }
    }
    SECTION("no retries when the policy says so") {
        ScriptedClient client;
        client.pushStatus(503);
        client.pushOk("never");
        CHECK(sendWithRetry(client, request, token, RetryPolicy{0, 0, 0}, "").status == AiStatus::ServerError);
        CHECK(client.count() == 1);
    }
    SECTION("network errors are not retried") {
        ScriptedClient client;
        client.pushError(HttpError::Connection, "refused");
        client.pushOk("never");
        const auto result = sendWithRetry(client, request, token, kFast, "");
        CHECK(result.status == AiStatus::NetworkError);
        CHECK(client.count() == 1);
    }
    SECTION("timeouts are reported as such") {
        ScriptedClient client;
        client.pushError(HttpError::Timeout, "slow");
        CHECK(sendWithRetry(client, request, token, kFast, "").status == AiStatus::Timeout);
    }
}

TEST_CASE("a Retry-After header sets the wait, capped by the policy", "[ai-http]") {
    HttpRequest request;
    request.url = "https://x.test/a";
    const CancellationToken token;
    ScriptedClient client;
    client.pushStatus(429, {}, 3600); // an hour: capped to maxDelayMs (5)
    client.pushOk("fine");
    const auto start = std::chrono::steady_clock::now();
    CHECK(sendWithRetry(client, request, token, kFast, "").ok());
    CHECK(std::chrono::steady_clock::now() - start < 2s);
}

TEST_CASE("the wait doubles with every retry and a Retry-After header replaces it", "[ai-http]") {
    HttpRequest request;
    request.url = "https://x.test/a";
    const CancellationToken token;
    {
        ScriptedClient client;
        client.pushStatus(503);
        client.pushStatus(503);
        client.pushOk("fine");
        const auto start = std::chrono::steady_clock::now();
        CHECK(sendWithRetry(client, request, token, RetryPolicy{2, 100, 5000}, "").ok());
        CHECK(std::chrono::steady_clock::now() - start >= 290ms); // 100 ms, then 200 ms
    }
    {
        ScriptedClient client;
        client.pushStatus(429, {}, 1);
        client.pushOk("fine");
        const auto start = std::chrono::steady_clock::now();
        CHECK(sendWithRetry(client, request, token, RetryPolicy{2, 1, 5000}, "").ok());
        CHECK(std::chrono::steady_clock::now() - start >= 950ms); // the header says one second
    }
}

TEST_CASE("a cancel during the backoff wait ends it at once", "[ai-http]") {
    HttpRequest request;
    request.url = "https://x.test/a";
    const CancellationToken token;
    ScriptedClient client;
    client.pushStatus(429);
    client.pushOk("never");
    std::thread canceller([&] {
        std::this_thread::sleep_for(80ms);
        token.cancel();
    });
    const auto start = std::chrono::steady_clock::now();
    const auto result = sendWithRetry(client, request, token, RetryPolicy{2, 30000, 30000}, "");
    canceller.join();
    CHECK(result.status == AiStatus::Cancelled);
    CHECK(std::chrono::steady_clock::now() - start < 3s);
    CHECK(client.count() == 1);
}

TEST_CASE("a token cancelled before the call sends nothing", "[ai-http]") {
    HttpRequest request;
    request.url = "https://x.test/a";
    const CancellationToken token;
    token.cancel();
    ScriptedClient client;
    CHECK(sendWithRetry(client, request, token, kFast, "").status == AiStatus::Cancelled);
    CHECK(client.count() == 0);
}

TEST_CASE("error messages are readable, short, clean and never hold the key", "[ai-http]") {
    HttpRequest request;
    request.url = "https://x.test/a";
    const CancellationToken token;
    SECTION("the message of the error object") {
        ScriptedClient client;
        client.pushStatus(401, R"({"error":{"message":"Incorrect API key provided: sk-SECRET."}})");
        const auto result = sendWithRetry(client, request, token, kFast, "sk-SECRET");
        CHECK(result.message == "Incorrect API key provided: ***.");
        CHECK(result.message.find("sk-SECRET") == std::string::npos);
    }
    SECTION("error as a string, and a top level message") {
        ScriptedClient a;
        a.pushStatus(404, R"({"error":"model 'x' not found"})");
        CHECK(sendWithRetry(a, request, token, kFast, "").message == "model 'x' not found");
        ScriptedClient b;
        b.pushStatus(400, R"({"message":"bad thing"})");
        CHECK(sendWithRetry(b, request, token, kFast, "").message == "bad thing");
    }
    SECTION("no usable body: the status") {
        ScriptedClient client;
        client.pushStatus(400, "<html>nope</html>");
        CHECK(sendWithRetry(client, request, token, kFast, "").message == "HTTP 400");
    }
    SECTION("long text is cut and control characters become spaces") {
        ScriptedClient client;
        client.pushStatus(400, json{{"error", {{"message", std::string("a\nb\tc") + std::string(1000, 'x')}}}}.dump());
        const auto message = sendWithRetry(client, request, token, kFast, "").message;
        CHECK(message.size() <= 304);
        CHECK(message.find('\n') == std::string::npos);
        CHECK(message.find('\t') == std::string::npos);
        CHECK(message.rfind("a b c", 0) == 0);
    }
    SECTION("the key in a connection error text") {
        ScriptedClient client;
        client.pushError(HttpError::Connection, "failed for https://u:sk-SECRET@host");
        CHECK(sendWithRetry(client, request, token, kFast, "sk-SECRET").message.find("sk-SECRET") == std::string::npos);
    }
}

TEST_CASE("the key does not travel over http to another computer", "[ai-http]") {
    ScriptedClient client;
    HttpRequest request;
    request.url = "http://example.com/v1";
    const auto result = sendWithRetry(client, request, CancellationToken(), kFast, "sk-SECRET");
    CHECK(result.status == AiStatus::BadRequest);
    CHECK(client.count() == 0);
}

TEST_CASE("anthropic: the request carries key, version, schema tool and settings", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(anthropicAnswer(patternAnswer()));
    AnthropicProvider provider(client, anthropicConfig());
    const auto result = provider.generate(sampleRequest(), CancellationToken());
    REQUIRE(result.ok());
    REQUIRE(client->count() == 1);
    const auto sent = client->requests().front();
    CHECK(sent.method == "POST");
    CHECK(sent.url == "https://api.anthropic.com/v1/messages");
    CHECK(header(sent, "x-api-key") == "sk-ant-SECRET");
    CHECK(header(sent, "anthropic-version") == "2023-06-01");
    CHECK(header(sent, "content-type") == "application/json");
    CHECK(sent.timeoutSeconds == 7);
    const auto body = json::parse(sent.body);
    CHECK(body["model"] == "some-model");
    CHECK(body["max_tokens"] == 1234);
    CHECK(body["temperature"] == 0.5);
    CHECK(body["system"] == "SYSTEM TEXT");
    CHECK(body["messages"][0]["role"] == "user");
    CHECK(body["messages"][0]["content"] == "USER TEXT");
    CHECK(body["tools"][0]["name"] == "submit_pattern");
    CHECK(body["tools"][0]["input_schema"] == json::parse(schemaV1Json()));
    CHECK(body["tool_choice"] == json({{"type", "tool"}, {"name", "submit_pattern"}}));
    CHECK(body.contains("stream") == false);
    CHECK(result.inputTokens == 11);
    CHECK(result.outputTokens == 22);
    CHECK(result.httpStatus == 200);
}

TEST_CASE("anthropic: the tool input is the answer text and runs through the pipeline", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(anthropicAnswer(patternAnswer()));
    AnthropicProvider provider(client, anthropicConfig());
    const auto profile = style();
    PromptTemplates templates;
    const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1", {"peak_time"});
    REQUIRE(loaded.missing.empty());
    templates = loaded.templates;
    AiGenerateInput input;
    input.style = &profile;
    input.templates = &templates;
    input.model = "m";
    const auto result = generateWithAi(provider, input, CancellationToken());
    CHECK(result.outcome == AiOutcome::Success);
    CHECK(result.inputTokens == 11);
}

TEST_CASE("anthropic: text block as fallback, nothing usable gives an empty text", "[ai-anthropic]") {
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(R"({"content":[{"type":"text","text":"{\"a\":1}"}]})");
        AnthropicProvider provider(client, anthropicConfig());
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        CHECK(result.ok());
        CHECK(result.text == "{\"a\":1}");
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(R"({"content":[]})");
        AnthropicProvider provider(client, anthropicConfig());
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        CHECK(result.ok());
        CHECK(result.text.empty());
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk("not json");
        AnthropicProvider provider(client, anthropicConfig());
        CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::BadRequest);
    }
}

TEST_CASE("anthropic: errors keep their status, nothing leaks the key, 429 is retried", "[ai-anthropic]") {
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(
            401,
            R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key sk-ant-SECRET"}})");
        AnthropicProvider provider(client, anthropicConfig());
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        CHECK(result.status == AiStatus::AuthFailed);
        CHECK(result.httpStatus == 401);
        CHECK(result.message.find("sk-ant-SECRET") == std::string::npos);
        CHECK(client->count() == 1);
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(404, R"({"error":{"type":"not_found_error","message":"model: nope"}})");
        AnthropicProvider provider(client, anthropicConfig());
        CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::ModelNotFound);
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(429);
        client->pushStatus(529);
        client->pushOk(anthropicAnswer(patternAnswer()));
        AnthropicProvider provider(client, anthropicConfig());
        CHECK(provider.generate(sampleRequest(), CancellationToken()).ok());
        CHECK(client->count() == 3);
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        AnthropicProvider provider(client, anthropicConfig());
        CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::NetworkError);
    }
}

TEST_CASE("anthropic: without a key nothing is sent", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    auto config = anthropicConfig();
    config.apiKey.clear();
    AnthropicProvider provider(client, config);
    CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::AuthFailed);
    CHECK(provider.testConnection(CancellationToken()).status == AiStatus::AuthFailed);
    CHECK(provider.listModels(CancellationToken()).empty());
    CHECK(client->count() == 0);
}

TEST_CASE("anthropic: a model that takes no temperature is asked again without it", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushStatus(400, R"({"error":{"message":"temperature is deprecated for this model"}})");
    client->pushOk(anthropicAnswer(patternAnswer()));
    AnthropicProvider provider(client, anthropicConfig());
    CHECK(provider.generate(sampleRequest(), CancellationToken()).ok());
    REQUIRE(client->count() == 2);
    CHECK(json::parse(client->requests()[0].body).contains("temperature"));
    CHECK_FALSE(json::parse(client->requests()[1].body).contains("temperature"));
}

TEST_CASE("anthropic: a bad request about something else is not asked again", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushStatus(400, R"({"error":{"message":"max_tokens too large"}})");
    client->pushOk("never");
    AnthropicProvider provider(client, anthropicConfig());
    CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::BadRequest);
    CHECK(client->count() == 1);
}

TEST_CASE("anthropic: the temperature is kept in 0 to 1 and a broken schema is refused", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(anthropicAnswer(patternAnswer()));
    AnthropicProvider provider(client, anthropicConfig());
    auto request = sampleRequest();
    request.temperature = 7.0;
    CHECK(provider.generate(request, CancellationToken()).ok());
    CHECK(json::parse(client->requests().front().body)["temperature"] == 1.0);
    request.schemaJson = "not json";
    CHECK(provider.generate(request, CancellationToken()).status == AiStatus::BadRequest);
    CHECK(client->count() == 1);
}

TEST_CASE("anthropic: connection test and model list use the models endpoint", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(R"({"data":[{"id":"claude-a"}]})");
    AnthropicProvider provider(client, anthropicConfig());
    const auto status = provider.testConnection(CancellationToken());
    CHECK(status.ok);
    CHECK(status.observed == SchemaSupport::Enforced);
    CHECK(client->requests().front().method == "GET");
    CHECK(client->requests().front().url == "https://api.anthropic.com/v1/models?limit=1");
    CHECK(header(client->requests().front(), "x-api-key") == "sk-ant-SECRET");

    client->pushOk(R"({"data":[{"id":"claude-b"},{"id":"claude-a"},{"nope":1}]})");
    const auto models = provider.listModels(CancellationToken());
    REQUIRE(models.size() == 2);
    CHECK(models[0] == "claude-b");

    client->pushStatus(401, R"({"error":{"message":"bad key"}})");
    const auto bad = provider.testConnection(CancellationToken());
    CHECK_FALSE(bad.ok);
    CHECK(bad.status == AiStatus::AuthFailed);
    client->pushStatus(500);
    CHECK(provider.listModels(CancellationToken()).empty());
}

TEST_CASE("anthropic: a cancel ends a running request", "[ai-anthropic]") {
    auto client = std::make_shared<ScriptedClient>();
    client->push(HttpResponse{HttpError::None, 200, anthropicAnswer(patternAnswer()), {}, 0}, 30000ms);
    AnthropicProvider provider(client, anthropicConfig());
    const CancellationToken token;
    std::thread canceller([&] {
        std::this_thread::sleep_for(60ms);
        token.cancel();
    });
    const auto start = std::chrono::steady_clock::now();
    CHECK(provider.generate(sampleRequest(), token).status == AiStatus::Cancelled);
    canceller.join();
    CHECK(std::chrono::steady_clock::now() - start < 3s);
}

TEST_CASE("anthropic: info", "[ai-anthropic]") {
    auto config = anthropicConfig();
    config.models = {"m1"};
    AnthropicProvider provider(std::make_shared<ScriptedClient>(), config);
    const auto info = provider.info();
    CHECK(info.id == "anthropic");
    CHECK(info.schemaSupport == SchemaSupport::Enforced);
    CHECK(info.models == std::vector<std::string>{"m1"});
}

TEST_CASE("openai: the request follows the level of the backend", "[ai-openai]") {
    const auto schema = json::parse(schemaV1Json());
    SECTION("enforced: json schema") {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(openAiAnswer("{}"));
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        REQUIRE(result.ok());
        CHECK(result.text == "{}");
        CHECK(result.inputTokens == 5);
        CHECK(result.outputTokens == 6);
        const auto sent = client->requests().front();
        CHECK(sent.url == "https://api.openai.com/v1/chat/completions");
        CHECK(header(sent, "authorization") == "Bearer sk-SECRET");
        const auto body = json::parse(sent.body);
        CHECK(body["response_format"]["type"] == "json_schema");
        CHECK(body["response_format"]["json_schema"]["schema"] == schema);
        CHECK(body["response_format"]["json_schema"]["strict"] == false);
        CHECK(body["messages"][0] == json({{"role", "system"}, {"content", "SYSTEM TEXT"}}));
        CHECK(body["messages"][1] == json({{"role", "user"}, {"content", "USER TEXT"}}));
        CHECK(body["max_completion_tokens"] == 1234);
        CHECK_FALSE(body.contains("max_tokens"));
        CHECK(body["temperature"] == 0.5 * 1.2);
    }
    SECTION("json only") {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(openAiAnswer("{}"));
        auto config = openAiConfig(SchemaSupport::JsonOnly);
        config.useMaxCompletionTokens = false;
        OpenAiProvider provider(client, config);
        REQUIRE(provider.generate(sampleRequest(), CancellationToken()).ok());
        const auto body = json::parse(client->requests().front().body);
        CHECK(body["response_format"] == json({{"type", "json_object"}}));
        CHECK(body["max_tokens"] == 1234);
        CHECK_FALSE(body.contains("max_completion_tokens"));
    }
    SECTION("prompt only") {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(openAiAnswer("{}"));
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::PromptOnly));
        REQUIRE(provider.generate(sampleRequest(), CancellationToken()).ok());
        CHECK_FALSE(json::parse(client->requests().front().body).contains("response_format"));
    }
}

TEST_CASE("openai: no key, no authorization header (Ollama), the base url keeps its path", "[ai-openai]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(openAiAnswer("{}"));
    auto config = openAiPreset(OpenAiPreset::Ollama);
    config.retry = kFast;
    OpenAiProvider provider(client, config);
    REQUIRE(provider.generate(sampleRequest(), CancellationToken()).ok());
    const auto sent = client->requests().front();
    CHECK(sent.url == "http://localhost:11434/v1/chat/completions");
    CHECK(header(sent, "authorization").empty());
    CHECK(provider.info().id == "ollama");
}

TEST_CASE("openai: a trailing slash of the base url is not doubled", "[ai-openai]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(openAiAnswer("{}"));
    auto config = openAiPreset(OpenAiPreset::Custom);
    config.baseUrl = "https://llm.example.com/api/v1/";
    OpenAiProvider provider(client, config);
    REQUIRE(provider.generate(sampleRequest(), CancellationToken()).ok());
    CHECK(client->requests().front().url == "https://llm.example.com/api/v1/chat/completions");
}

TEST_CASE("openai: a custom address is limited to json only, the presets have their levels", "[ai-openai]") {
    CHECK(openAiPreset(OpenAiPreset::Custom).schemaSupport == SchemaSupport::JsonOnly);
    CHECK(openAiPreset(OpenAiPreset::OpenAi).schemaSupport == SchemaSupport::Enforced);
    CHECK(openAiPreset(OpenAiPreset::Ollama).schemaSupport == SchemaSupport::Enforced);
    CHECK(openAiPreset(OpenAiPreset::OpenRouter).schemaSupport == SchemaSupport::JsonOnly);
    CHECK(openAiPreset(OpenAiPreset::LmStudio).baseUrl == "http://localhost:1234/v1");
    CHECK(openAiPreset(OpenAiPreset::OpenRouter).baseUrl == "https://openrouter.ai/api/v1");
}

TEST_CASE("openai: errors keep their status and the key stays out of the message", "[ai-openai]") {
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(401, R"({"error":{"message":"Incorrect API key provided: sk-SECRET"}})");
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        CHECK(result.status == AiStatus::AuthFailed);
        CHECK(result.message.find("sk-SECRET") == std::string::npos);
        CHECK(client->count() == 1);
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(404, R"({"error":{"message":"The model `x` does not exist"}})");
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::ModelNotFound);
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(503);
        client->pushStatus(429);
        client->pushOk(openAiAnswer("{}"));
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        CHECK(provider.generate(sampleRequest(), CancellationToken()).ok());
        CHECK(client->count() == 3);
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushError(HttpError::Connection, "connection refused");
        OpenAiProvider provider(client, openAiPreset(OpenAiPreset::Ollama));
        CHECK(provider.generate(sampleRequest(), CancellationToken()).status == AiStatus::NetworkError);
    }
}

TEST_CASE("openai: odd answers give an empty text, not a crash", "[ai-openai]") {
    for (const std::string body : {R"({"choices":[]})", R"({"choices":[{"message":{"content":null}}]})",
                                   R"({"choices":[{"nomessage":1}]})", R"({"choices":"x"})", R"({})", R"([])"}) {
        INFO(body);
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(body);
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        if (body == "[]") {
            CHECK(result.status == AiStatus::BadRequest);
        } else {
            CHECK(result.ok());
            CHECK(result.text.empty());
        }
    }
}

TEST_CASE("openai: an answer in the reasoning field of a reasoning model is used when the content is empty",
          "[ai-openai]") {
    for (const std::string field : {"reasoning_content", "reasoning"}) {
        INFO(field);
        auto client = std::make_shared<ScriptedClient>();
        json message = {{"content", ""}, {field, "{\"a\":1}"}};
        client->pushOk(json{{"choices", json::array({json{{"message", message}}})}}.dump());
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        const auto result = provider.generate(sampleRequest(), CancellationToken());
        CHECK(result.ok());
        CHECK(result.text == "{\"a\":1}");
    }
    // The content wins when there is one, and a reasoning text without content is not invented from nothing.
    auto client = std::make_shared<ScriptedClient>();
    json message = {{"content", "{\"b\":2}"}, {"reasoning_content", "thinking"}};
    client->pushOk(json{{"choices", json::array({json{{"message", message}}})}}.dump());
    OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
    CHECK(provider.generate(sampleRequest(), CancellationToken()).text == "{\"b\":2}");
}

TEST_CASE("openai: a model that takes no temperature is asked again without it", "[ai-openai]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushStatus(
        400, R"({"error":{"message":"Unsupported value: 'temperature' does not support 0.6 with this model."}})");
    client->pushOk(openAiAnswer("{}"));
    OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
    CHECK(provider.generate(sampleRequest(), CancellationToken()).ok());
    REQUIRE(client->count() == 2);
    CHECK_FALSE(json::parse(client->requests()[1].body).contains("temperature"));
}

TEST_CASE("openai: the connection test can only lower the level", "[ai-openai]") {
    const auto test = [](SchemaSupport configured, const std::string& content) {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(openAiAnswer(content));
        OpenAiProvider provider(client, openAiConfig(configured));
        const auto status = provider.testConnection(CancellationToken());
        REQUIRE(client->count() == 1);
        CHECK(json::parse(client->requests().front().body)["model"] == "gpt-test");
        return status;
    };
    SECTION("a backend that enforces the schema keeps the level") {
        const auto status = test(SchemaSupport::Enforced, R"({"answer":"cat"})");
        CHECK(status.ok);
        CHECK(status.observed == SchemaSupport::Enforced);
    }
    SECTION("json that ignores the schema lowers enforced to json only") {
        CHECK(test(SchemaSupport::Enforced, R"({"answer":"dog"})").observed == SchemaSupport::JsonOnly);
    }
    SECTION("no json lowers to prompt only") {
        CHECK(test(SchemaSupport::Enforced, "Sure! A dog.").observed == SchemaSupport::PromptOnly);
        CHECK(test(SchemaSupport::JsonOnly, "Sure! A dog.").observed == SchemaSupport::PromptOnly);
    }
    SECTION("a good answer never raises the level") {
        CHECK(test(SchemaSupport::JsonOnly, R"({"answer":"cat"})").observed == SchemaSupport::JsonOnly);
        CHECK(test(SchemaSupport::PromptOnly, R"({"answer":"cat"})").observed == SchemaSupport::PromptOnly);
    }
    SECTION("an error is reported with its status") {
        auto client = std::make_shared<ScriptedClient>();
        client->pushStatus(401, R"({"error":{"message":"bad key"}})");
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        const auto status = provider.testConnection(CancellationToken());
        CHECK_FALSE(status.ok);
        CHECK(status.status == AiStatus::AuthFailed);
        CHECK(client->count() == 1); // the test never retries
    }
    SECTION("without a chosen model there is nothing to test") {
        auto client = std::make_shared<ScriptedClient>();
        auto config = openAiConfig(SchemaSupport::Enforced);
        config.testModel.clear();
        OpenAiProvider provider(client, config);
        CHECK(provider.testConnection(CancellationToken()).status == AiStatus::ModelNotFound);
        CHECK(client->count() == 0);
    }
}

TEST_CASE("openai: the model list is loaded from the provider, sorted, chat models only for OpenAI", "[ai-openai]") {
    const std::string body =
        R"({"data":[{"id":"gpt-b"},{"id":"text-embedding-3-small"},{"id":"gpt-a"},{"id":"whisper-1"},{"x":1}]})";
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(body);
        OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
        const auto models = provider.listModels(CancellationToken());
        CHECK(models == std::vector<std::string>{"gpt-a", "gpt-b"});
        CHECK(client->requests().front().method == "GET");
        CHECK(client->requests().front().url == "https://api.openai.com/v1/models");
        CHECK(header(client->requests().front(), "authorization") == "Bearer sk-SECRET");
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushOk(body);
        auto config = openAiPreset(OpenAiPreset::Ollama);
        OpenAiProvider provider(client, config);
        CHECK(provider.listModels(CancellationToken()).size() == 4); // installed models are all shown
    }
    {
        auto client = std::make_shared<ScriptedClient>();
        client->pushError(HttpError::Connection);
        OpenAiProvider provider(client, openAiPreset(OpenAiPreset::Ollama));
        CHECK(provider.listModels(CancellationToken()).empty());
    }
}

TEST_CASE("openai: a cancel ends a running request", "[ai-openai]") {
    auto client = std::make_shared<ScriptedClient>();
    client->push(HttpResponse{HttpError::None, 200, openAiAnswer("{}"), {}, 0}, 30000ms);
    OpenAiProvider provider(client, openAiConfig(SchemaSupport::Enforced));
    const CancellationToken token;
    std::thread canceller([&] {
        std::this_thread::sleep_for(60ms);
        token.cancel();
    });
    const auto start = std::chrono::steady_clock::now();
    CHECK(provider.generate(sampleRequest(), token).status == AiStatus::Cancelled);
    canceller.join();
    CHECK(std::chrono::steady_clock::now() - start < 3s);
}

TEST_CASE("openai: an answer through the pipeline gives a valid pattern", "[ai-openai]") {
    auto client = std::make_shared<ScriptedClient>();
    client->pushOk(openAiAnswer("```json\n" + patternAnswer() + "\n```"));
    OpenAiProvider provider(client, openAiConfig(SchemaSupport::JsonOnly));
    const auto profile = style();
    const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1", {"peak_time"});
    REQUIRE(loaded.missing.empty());
    AiGenerateInput input;
    input.style = &profile;
    input.templates = &loaded.templates;
    input.model = "m";
    const auto result = generateWithAi(provider, input, CancellationToken());
    CHECK(result.outcome == AiOutcome::Success);
    CHECK(result.pattern.info.providerId == "openai");
}
