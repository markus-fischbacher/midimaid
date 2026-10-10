#include "FakeHttpServer.h"
#include "ai/AiGeneration.h"
#include "ai/AnthropicProvider.h"
#include "ai/OpenAiProvider.h"
#include "ai/PatternCompact.h"
#include "core/PatternGenerator.h"
#include "core/StyleProfile.h"
#include "plugin/JuceHttpClient.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <thread>

using namespace mm::ai;
using namespace mm::test;
using namespace std::chrono_literals;
using nlohmann::json;

namespace {

HttpRequest requestTo(const FakeHttpServer& server, const std::string& path = "/v1/test") {
    HttpRequest request;
    request.url = server.url(path);
    request.headers = {{"content-type", "application/json"}, {"x-api-key", "k-123"}};
    request.body = R"({"text":"Grüße – ünïcode"})";
    request.connectTimeoutSeconds = 5;
    request.timeoutSeconds = 10;
    return request;
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

AiGenerateResult runPipeline(IAiProvider& provider) {
    static const auto profile = style();
    static const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1", {"peak_time"});
    AiGenerateInput input;
    input.style = &profile;
    input.templates = &loaded.templates;
    input.model = "m";
    input.timeoutSeconds = 10;
    return generateWithAi(provider, input, CancellationToken());
}

} // namespace

TEST_CASE("a POST arrives with method, path, headers and the exact body", "[net]") {
    FakeHttpServer server([](const FakeRequest&) { return FakeReply{200, R"({"ok":true})", {}, 0, false}; });
    mm::plugin::JuceHttpClient client;
    const auto request = requestTo(server);
    const auto response = client.send(request, CancellationToken());
    REQUIRE(response.error == HttpError::None);
    CHECK(response.status == 200);
    CHECK(response.body == R"({"ok":true})");
    const auto seen = server.requests();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].method == "POST");
    CHECK(seen[0].path == "/v1/test");
    CHECK(seen[0].headers.at("x-api-key") == "k-123");
    CHECK(seen[0].headers.at("content-type") == "application/json");
    CHECK(seen[0].body == request.body);
}

TEST_CASE("a GET has no body", "[net]") {
    FakeHttpServer server([](const FakeRequest&) { return FakeReply{200, "[]", {}, 0, false}; });
    mm::plugin::JuceHttpClient client;
    auto request = requestTo(server, "/v1/models?limit=1");
    request.method = "GET";
    request.body.clear();
    const auto response = client.send(request, CancellationToken());
    REQUIRE(response.error == HttpError::None);
    CHECK(response.body == "[]");
    const auto seen = server.requests();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].method == "GET");
    CHECK(seen[0].path == "/v1/models?limit=1");
    CHECK(seen[0].body.empty());
}

TEST_CASE("an error status brings its body and Retry-After", "[net]") {
    FakeHttpServer server([](const FakeRequest&) {
        return FakeReply{429, R"({"error":{"message":"slow down"}})", {{"Retry-After", "7"}}, 0, false};
    });
    mm::plugin::JuceHttpClient client;
    const auto response = client.send(requestTo(server), CancellationToken());
    CHECK(response.error == HttpError::None);
    CHECK(response.status == 429);
    CHECK(response.body == R"({"error":{"message":"slow down"}})");
    CHECK(response.retryAfterSeconds == 7);
}

TEST_CASE("a large answer arrives complete", "[net]") {
    const std::string big(1024 * 1024, 'x');
    FakeHttpServer server([&](const FakeRequest&) { return FakeReply{200, big, {}, 0, false}; });
    mm::plugin::JuceHttpClient client;
    const auto response = client.send(requestTo(server), CancellationToken());
    CHECK(response.error == HttpError::None);
    CHECK(response.body.size() == big.size());
}

TEST_CASE("a refused connection is a connection error, quickly", "[net]") {
    int port = 0;
    {
        FakeHttpServer server([](const FakeRequest&) { return FakeReply{}; });
        port = server.port();
    } // closed again: nothing listens on the port
    mm::plugin::JuceHttpClient client;
    HttpRequest request;
    request.url = "http://127.0.0.1:" + std::to_string(port) + "/x";
    request.body = "{}";
    request.connectTimeoutSeconds = 5;
    const auto start = std::chrono::steady_clock::now();
    const auto response = client.send(request, CancellationToken());
    CHECK(response.error == HttpError::Connection);
    CHECK(std::chrono::steady_clock::now() - start < 5s);
}

TEST_CASE("a server that never answers ends with a timeout", "[net]") {
    FakeHttpServer server([](const FakeRequest&) { return FakeReply{200, "{}", {}, 20000, false}; });
    mm::plugin::JuceHttpClient client;
    auto request = requestTo(server);
    request.timeoutSeconds = 1;
    const auto start = std::chrono::steady_clock::now();
    const auto response = client.send(request, CancellationToken());
    CHECK(response.error == HttpError::Timeout);
    CHECK(std::chrono::steady_clock::now() - start < 6s);
}

TEST_CASE("an answer that stops half way is an error, not a short answer", "[net]") {
    FakeHttpServer server([](const FakeRequest&) {
        FakeReply reply;
        reply.body = std::string(1000, 'x');
        reply.partialBytes = 100;
        reply.delayMs = 20000;
        return reply;
    });
    mm::plugin::JuceHttpClient client;
    auto request = requestTo(server);
    request.timeoutSeconds = 1;
    const auto response = client.send(request, CancellationToken());
    CHECK(response.error == HttpError::Timeout);
}

TEST_CASE("a cancel ends the open connection at once", "[net]") {
    FakeHttpServer server([](const FakeRequest&) { return FakeReply{200, "{}", {}, 20000, false}; });
    mm::plugin::JuceHttpClient client;
    const CancellationToken token;
    std::thread canceller([&] {
        std::this_thread::sleep_for(150ms);
        token.cancel();
    });
    const auto start = std::chrono::steady_clock::now();
    const auto response = client.send(requestTo(server), token);
    canceller.join();
    CHECK(response.error == HttpError::Cancelled);
    CHECK(std::chrono::steady_clock::now() - start < 3s);
}

TEST_CASE("a token cancelled before the call sends nothing", "[net]") {
    FakeHttpServer server([](const FakeRequest&) { return FakeReply{}; });
    mm::plugin::JuceHttpClient client;
    const CancellationToken token;
    token.cancel();
    CHECK(client.send(requestTo(server), token).error == HttpError::Cancelled);
    CHECK(server.requests().empty());
}

TEST_CASE("a server that hangs up gives an error, not an answer", "[net]") {
    FakeHttpServer server([](const FakeRequest&) {
        FakeReply reply;
        reply.hangUp = true;
        return reply;
    });
    mm::plugin::JuceHttpClient client;
    const auto response = client.send(requestTo(server), CancellationToken());
    CHECK((response.error != HttpError::None || response.status == 0));
    CHECK_FALSE((response.status >= 200 && response.status < 300));
}

TEST_CASE("redirects are not followed: the key stays with the host it was meant for", "[net]") {
    FakeHttpServer target([](const FakeRequest&) { return FakeReply{200, "{}", {}, 0, false}; });
    FakeHttpServer server(
        [&](const FakeRequest&) { return FakeReply{302, "", {{"Location", target.url("/stolen")}}, 0, false}; });
    mm::plugin::JuceHttpClient client;
    const auto response = client.send(requestTo(server), CancellationToken());
    CHECK(response.status == 302);
    CHECK(target.requests().empty());
}

TEST_CASE("anthropic through the real client: a pattern comes back, with the key in the header only", "[net]") {
    FakeHttpServer server([](const FakeRequest&) {
        const auto answer =
            json{{"content", json::array({{{"type", "tool_use"}, {"input", json::parse(patternAnswer())}}})},
                 {"usage", {{"input_tokens", 3}, {"output_tokens", 4}}}};
        return FakeReply{200, answer.dump(), {}, 0, false};
    });
    AnthropicConfig config;
    config.baseUrl = server.url();
    config.apiKey = "sk-ant-SECRET";
    AnthropicProvider provider(std::make_shared<mm::plugin::JuceHttpClient>(), config);
    const auto result = runPipeline(provider);
    CHECK(result.outcome == AiOutcome::Success);
    CHECK(result.inputTokens == 3);
    const auto seen = server.requests();
    REQUIRE(seen.size() == 1);
    CHECK(seen[0].path == "/v1/messages");
    CHECK(seen[0].headers.at("x-api-key") == "sk-ant-SECRET");
    CHECK(seen[0].headers.at("anthropic-version") == "2023-06-01");
    CHECK(seen[0].body.find("sk-ant-SECRET") == std::string::npos);
}

TEST_CASE("openai through the real client: 429 is retried, then the answer counts", "[net]") {
    std::atomic<int> calls{0};
    FakeHttpServer server([&](const FakeRequest&) {
        if (calls.fetch_add(1) == 0) {
            return FakeReply{429, R"({"error":{"message":"busy"}})", {{"Retry-After", "1"}}, 0, false};
        }
        const auto answer = json{{"choices", json::array({{{"message", {{"content", patternAnswer()}}}}})}};
        return FakeReply{200, answer.dump(), {}, 0, false};
    });
    auto config = openAiPreset(OpenAiPreset::Custom);
    config.baseUrl = server.url("/v1");
    config.apiKey = "sk-SECRET";
    OpenAiProvider provider(std::make_shared<mm::plugin::JuceHttpClient>(), config);
    const auto result = runPipeline(provider);
    CHECK(result.outcome == AiOutcome::Success);
    CHECK(calls.load() == 2);
    CHECK(server.requests()[0].path == "/v1/chat/completions");
    CHECK(server.requests()[0].headers.at("authorization") == "Bearer sk-SECRET");
}

TEST_CASE("a wrong key is an auth error without a retry and without the key in the text", "[net]") {
    FakeHttpServer server([](const FakeRequest&) {
        return FakeReply{401, R"({"error":{"message":"bad key sk-SECRET"}})", {}, 0, false};
    });
    auto config = openAiPreset(OpenAiPreset::Custom);
    config.baseUrl = server.url("/v1");
    config.apiKey = "sk-SECRET";
    OpenAiProvider provider(std::make_shared<mm::plugin::JuceHttpClient>(), config);
    const auto result = runPipeline(provider);
    CHECK(result.outcome == AiOutcome::ProviderError);
    CHECK(result.status == AiStatus::AuthFailed);
    CHECK(result.error.find("sk-SECRET") == std::string::npos);
    CHECK(server.requests().size() == 1);
}

TEST_CASE("ollama not running is a network error with a readable cause", "[net]") {
    int port = 0;
    {
        FakeHttpServer server([](const FakeRequest&) { return FakeReply{}; });
        port = server.port();
    }
    auto config = openAiPreset(OpenAiPreset::Ollama);
    config.baseUrl = "http://127.0.0.1:" + std::to_string(port) + "/v1";
    OpenAiProvider provider(std::make_shared<mm::plugin::JuceHttpClient>(), config);
    const auto result = runPipeline(provider);
    CHECK(result.outcome == AiOutcome::ProviderError);
    CHECK(result.status == AiStatus::NetworkError);
}

TEST_CASE("a cancel during a slow provider ends the pipeline quickly", "[net]") {
    FakeHttpServer server([](const FakeRequest&) { return FakeReply{200, "{}", {}, 20000, false}; });
    auto config = openAiPreset(OpenAiPreset::Custom);
    config.baseUrl = server.url("/v1");
    OpenAiProvider provider(std::make_shared<mm::plugin::JuceHttpClient>(), config);
    const CancellationToken token;
    std::thread canceller([&] {
        std::this_thread::sleep_for(200ms);
        token.cancel();
    });
    static const auto profile = style();
    static const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1", {"peak_time"});
    AiGenerateInput input;
    input.style = &profile;
    input.templates = &loaded.templates;
    input.model = "m";
    const auto start = std::chrono::steady_clock::now();
    const auto result = generateWithAi(provider, input, token);
    canceller.join();
    CHECK(result.outcome == AiOutcome::Cancelled);
    CHECK(std::chrono::steady_clock::now() - start < 3s);
}
