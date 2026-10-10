// Practice test with a real provider (ROADMAP P3-G). Hidden from ctest: it needs a running backend or a key.
//
//   MM_LIVE_PROVIDER  lmstudio (default) | ollama | openai | anthropic | openrouter
//   MM_LIVE_MODEL     the model id (required)
//   MM_LIVE_API_KEY   the key, for the providers that need one; read here, never printed or stored
//   MM_LIVE_BASE_URL  optional: another address than the preset's
//   MM_LIVE_TIMEOUT   optional: seconds per request (default 300)
//   MM_LIVE_STYLE     optional: style id (default peak_time)
//
//   mm_live_tests "[.live]"
#include "ai/AiGeneration.h"
#include "ai/AiRefine.h"
#include "ai/AnthropicProvider.h"
#include "ai/OpenAiProvider.h"
#include "core/PatternValidation.h"
#include "core/StyleProfile.h"
#include "plugin/JuceHttpClient.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

using namespace mm::ai;
using namespace mm::core;
using namespace std::chrono_literals;

namespace {

std::string env(const char* name, const std::string& fallback = {}) {
    const char* value = std::getenv(name);
    return value != nullptr ? value : fallback;
}

std::shared_ptr<IAiProvider> makeProvider(const std::string& model) {
    const std::string kind = env("MM_LIVE_PROVIDER", "lmstudio");
    const std::string key = env("MM_LIVE_API_KEY");
    const std::string baseUrl = env("MM_LIVE_BASE_URL");
    auto client = std::make_shared<mm::plugin::JuceHttpClient>();
    if (kind == "anthropic") {
        AnthropicConfig config;
        config.apiKey = key;
        if (!baseUrl.empty()) {
            config.baseUrl = baseUrl;
        }
        return std::make_shared<AnthropicProvider>(client, config);
    }
    OpenAiPreset preset = OpenAiPreset::LmStudio;
    if (kind == "ollama") {
        preset = OpenAiPreset::Ollama;
    } else if (kind == "openai") {
        preset = OpenAiPreset::OpenAi;
    } else if (kind == "openrouter") {
        preset = OpenAiPreset::OpenRouter;
    }
    auto config = openAiPreset(preset);
    config.apiKey = key;
    config.testModel = model;
    if (!baseUrl.empty()) {
        config.baseUrl = baseUrl;
    }
    return std::make_shared<OpenAiProvider>(client, config);
}

StyleProfile shipped(const std::string& id) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + id + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

long long millisSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
}

void report(const char* step, const AiGenerateResult& result, long long ms) {
    std::printf("[live] %-8s outcome=%d status=%d http=%d requests=%d repaired=%d score=%d below=%d tokens=%d/%d "
                "dropped=%zu clamped=%zu (%lld ms)\n",
                step, static_cast<int>(result.outcome), static_cast<int>(result.status), result.httpStatus,
                result.requests, result.repaired, result.score, result.belowMinScore, result.inputTokens,
                result.outputTokens, result.droppedNotes, result.clampedValues, ms);
    if (result.outcome != AiOutcome::Success) {
        std::printf("[live] %-8s error=%s\n[live] last answer (first 600): %.600s\n", step, result.error.c_str(),
                    result.lastAnswer.c_str());
    }
}

/// How many notes of `after` are new or changed against `before` (by id and values), for each voice.
void changes(const char* step, const Pattern& before, const Pattern& after) {
    std::printf("[live] %-8s changed:", step);
    for (size_t v = 0; v < after.voices.size() && v < before.voices.size(); ++v) {
        size_t changed = 0;
        for (const auto& note : after.voices[v].notes) {
            const auto& old = before.voices[v].notes;
            changed += std::find(old.begin(), old.end(), note) == old.end() ? 1 : 0;
        }
        std::printf(" %s=%zu", after.voices[v].role == VoiceRole::Bass ? "bass" : "melody", changed);
    }
    std::printf("\n");
}

void notes(const char* step, const Pattern& pattern) {
    std::printf("[live] %-8s notes:", step);
    for (const auto& track : pattern.voices) {
        std::printf(" %s=%zu", track.role == VoiceRole::Bass ? "bass" : "melody", track.notes.size());
    }
    std::printf("\n");
}

} // namespace

TEST_CASE("live: a real provider generates and refines valid patterns", "[.live]") {
    const std::string model = env("MM_LIVE_MODEL");
    REQUIRE_FALSE(model.empty()); // MM_LIVE_MODEL names the model
    const int timeout = std::atoi(env("MM_LIVE_TIMEOUT", "300").c_str());
    const auto style = shipped(env("MM_LIVE_STYLE", "peak_time"));
    const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1",
                                            {"peak_time", "melodic_techno", "hard_industrial"});
    REQUIRE(loaded.missing.empty());
    const auto provider = makeProvider(model);
    std::printf("\n[live] provider=%s model=%s timeout=%d s style=%s\n", provider->info().id.c_str(), model.c_str(),
                timeout, style.id.c_str());

    auto start = std::chrono::steady_clock::now();
    const auto connection = provider->testConnection(CancellationToken());
    std::printf("[live] connection ok=%d status=%d level=%d (%lld ms) %s\n", connection.ok,
                static_cast<int>(connection.status), static_cast<int>(connection.observed), millisSince(start),
                connection.message.c_str());
    REQUIRE(connection.ok);

    AiGenerateInput generate;
    generate.style = &style;
    generate.templates = &loaded.templates;
    generate.model = model;
    generate.timeoutSeconds = timeout;
    generate.maxTokens = 6000;
    generate.prompt.request = "dark rolling bass, hypnotic short melody";
    generate.prompt.lengthBars = 4;
    start = std::chrono::steady_clock::now();
    const auto made = generateWithAi(*provider, generate, CancellationToken());
    report("generate", made, millisSince(start));
    REQUIRE(made.outcome == AiOutcome::Success);
    CHECK(validatePattern(made.pattern).empty());
    notes("generate", made.pattern);
    for (const auto& track : made.pattern.voices) {
        CHECK(track.notes.size() >= 4); // a voice with fewer notes than a bar's worth is not a pattern
    }

    AiRefineInput refineMelody;
    refineMelody.style = &style;
    refineMelody.templates = &loaded.templates;
    refineMelody.pattern = made.pattern;
    refineMelody.instruction = "fewer notes and more space in the melody";
    refineMelody.voice = 1;
    refineMelody.model = model;
    refineMelody.timeoutSeconds = timeout;
    refineMelody.maxTokens = 6000;
    start = std::chrono::steady_clock::now();
    const auto refined = refineWithAi(*provider, refineMelody, CancellationToken());
    report("refine", refined, millisSince(start));
    REQUIRE(refined.outcome == AiOutcome::Success);
    CHECK(validatePattern(refined.pattern).empty());
    notes("refine", refined.pattern);
    changes("refine", made.pattern, refined.pattern);
    CHECK(refined.pattern.voices[0] == made.pattern.voices[0]); // the bass was out of the scope
    CHECK(refined.pattern.refineHistory.size() == 1);

    AiRefineInput refineAgain = refineMelody;
    refineAgain.pattern = refined.pattern;
    refineAgain.instruction = "now add a few slides to the bass";
    refineAgain.voice = 0;
    start = std::chrono::steady_clock::now();
    const auto again = refineWithAi(*provider, refineAgain, CancellationToken());
    report("refine 2", again, millisSince(start));
    REQUIRE(again.outcome == AiOutcome::Success);
    CHECK(validatePattern(again.pattern).empty());
    CHECK(again.pattern.voices[1] == refined.pattern.voices[1]);
    CHECK(again.pattern.refineHistory.size() == 2);
    notes("refine 2", again.pattern);
    changes("refine 2", refined.pattern, again.pattern);
}

TEST_CASE("live: cancel ends a running request quickly", "[.live]") {
    const std::string model = env("MM_LIVE_MODEL");
    REQUIRE_FALSE(model.empty());
    const auto style = shipped(env("MM_LIVE_STYLE", "peak_time"));
    const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1",
                                            {"peak_time", "melodic_techno", "hard_industrial"});
    const auto provider = makeProvider(model);
    AiGenerateInput input;
    input.style = &style;
    input.templates = &loaded.templates;
    input.model = model;
    input.timeoutSeconds = 300;
    input.maxTokens = 6000;
    input.prompt.lengthBars = 8;
    CancellationToken token;
    std::atomic<bool> done{false};
    AiGenerateResult result;
    std::thread worker([&] {
        result = generateWithAi(*provider, input, token);
        done = true;
    });
    std::this_thread::sleep_for(1500ms);
    const auto cancelled = std::chrono::steady_clock::now();
    token.cancel();
    while (!done && millisSince(cancelled) < 10000) {
        std::this_thread::sleep_for(20ms);
    }
    const auto took = millisSince(cancelled);
    std::printf("[live] cancel finished=%d outcome=%d after %lld ms\n", done.load(), static_cast<int>(result.outcome),
                took);
    CHECK(done.load());
    CHECK(took < 3000);
    if (worker.joinable()) {
        worker.join();
    }
    CHECK(result.outcome == AiOutcome::Cancelled);
}
