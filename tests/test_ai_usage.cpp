#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"
#include "core/PatternGenerator.h"
#include "plugin/AiBackend.h"
#include "plugin/AiUsage.h"
#include "plugin/EmbeddedPrompts.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>

using namespace mm::plugin;
using Catch::Approx;

namespace {

struct TempFolder {
    juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("mm_usage_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    TempFolder() { dir.createDirectory(); }
    ~TempFolder() { dir.deleteRecursively(); }
    juce::File file() const { return dir.getChildFile("pricing.json"); }
};

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 10000) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) {
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

} // namespace

TEST_CASE("a cloud request gets tokens and a cost from the shipped prices", "[ai-usage]") {
    TempFolder temp; // no pricing file in it
    AiUsage usage(temp.file());
    const auto entry = usage.record("claude-sonnet-5-5", true, 1000, 2000);
    CHECK(entry.inputTokens == 1000);
    CHECK(entry.outputTokens == 2000);
    REQUIRE(entry.cost.has_value());
    CHECK(*entry.cost > 0.0);
    CHECK(*entry.cost < 1.0);
    CHECK(usage.session().requests == 1);
    CHECK(usage.session().tokens() == 3000);
    CHECK(usage.session().cost == Approx(*entry.cost));
}

TEST_CASE("a local provider or an unknown model has tokens but no cost", "[ai-usage]") {
    TempFolder temp;
    AiUsage usage(temp.file());
    const auto local = usage.record("claude-sonnet-5-5", false, 500, 500); // priced model, but a local provider
    CHECK_FALSE(local.cost.has_value());
    const auto unknown = usage.record("qwen3-27b", true, 100, 100);
    CHECK_FALSE(unknown.cost.has_value());
    CHECK(usage.session().requests == 2);
    CHECK(usage.session().tokens() == 1200);
    CHECK(usage.session().unpriced == 2);
    CHECK(usage.session().cost == Approx(0.0));
}

TEST_CASE("the pricing file of the user is read and read again when it changes", "[ai-usage]") {
    TempFolder temp;
    AiUsage usage(temp.file());
    CHECK_FALSE(usage.record("my-model", true, 1000000, 0).cost.has_value());

    REQUIRE(temp.file().replaceWithText(
        R"({"currency": "EUR", "models": [{"model": "my-model", "inputPerMillion": 2, "outputPerMillion": 4}]})"));
    temp.file().setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(5));
    const auto first = usage.record("my-model", true, 1000000, 500000);
    REQUIRE(first.cost.has_value());
    CHECK(*first.cost == Approx(4.0));
    CHECK(usage.pricing().currency == "EUR");

    REQUIRE(temp.file().replaceWithText(
        R"({"currency": "EUR", "models": [{"model": "my-model", "inputPerMillion": 20, "outputPerMillion": 40}]})"));
    temp.file().setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::seconds(10));
    CHECK(*usage.record("my-model", true, 1000000, 0).cost == Approx(20.0));

    temp.file().deleteFile(); // back to the shipped prices
    CHECK_FALSE(usage.record("my-model", true, 1000000, 0).cost.has_value());
    CHECK(usage.pricing().currency == "USD");
}

TEST_CASE("the session can be reset", "[ai-usage]") {
    TempFolder temp;
    AiUsage usage(temp.file());
    usage.record("gpt-4o", true, 10, 10);
    usage.resetSession();
    CHECK(usage.session() == mm::ai::UsageTally{});
}

TEST_CASE("the status text names the request, the session and the estimated cost", "[ai-usage]") {
    mm::ai::UsageTally session;
    session = mm::ai::addUsage(session, 3000, 400, 0.04);
    const auto priced = usageStatusText({900, 300, 0.0123}, session, "USD");
    CHECK(priced.find("Tokens 1,2k") != std::string::npos);
    CHECK(priced.find("~0,01 USD") != std::string::npos);
    CHECK(priced.find("Sitzung: 3,4k") != std::string::npos);
    CHECK(priced.find("~0,04") != std::string::npos);
    CHECK(priced.find("geschätzt") != std::string::npos);

    const auto local = usageStatusText({900, 300, std::nullopt}, session, "USD");
    CHECK(local.find("Tokens 1,2k") != std::string::npos);
    CHECK(local.find("USD") == std::string::npos);
    CHECK(local.find("Sitzung: 3,4k") != std::string::npos);

    session = mm::ai::addUsage(session, 10, 10, std::nullopt);
    CHECK(usageStatusText({900, 300, 0.5}, session, "USD").find("~0,04+") != std::string::npos);
    CHECK(usageStatusText({0, 0, std::nullopt}, session, "USD").empty());
}

TEST_CASE("an AI generation reports its tokens and the editor shows them", "[ai-usage]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto* style = processor.styles().find("peak_time");
    REQUIRE(style != nullptr);
    mm::core::GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 5;
    const auto generated = mm::core::generatePattern(*style, request);
    REQUIRE(generated.success);
    auto provider = std::make_shared<mm::ai::MockProvider>();
    mm::ai::AiResult answer;
    answer.status = mm::ai::AiStatus::Ok;
    answer.text = mm::ai::patternToSchemaJson(generated.pattern, style, false)->json;
    answer.inputTokens = 1500;
    answer.outputTokens = 700;
    provider->enqueue(answer);
    AiBackendSettings settings;
    settings.provider = provider;
    settings.model = "mock-model";
    ScopedAiBackend backend(settings);

    const auto before = AiUsage::instance().session();
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() != GenerationStatus::Generating; }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Done);

    CHECK(processor.lastReport().usage.inputTokens == 1500);
    CHECK(processor.lastReport().usage.outputTokens == 700);
    CHECK_FALSE(processor.lastReport().usage.cost.has_value()); // the mock is not a cloud provider
    const auto after = AiUsage::instance().session();
    CHECK(after.requests == before.requests + 1);
    CHECK(after.tokens() == before.tokens() + 2200);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(400); // the editor's timer
    const auto text = editor->findChildWithID("status");
    REQUIRE(text != nullptr);
    CHECK(static_cast<juce::Label*>(text)->getText().contains("Tokens 2,2k"));
}
