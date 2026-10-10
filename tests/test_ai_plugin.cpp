#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"
#include "core/PatternGenerator.h"
#include "plugin/AiBackend.h"
#include "plugin/EmbeddedPrompts.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <memory>

using namespace mm::plugin;
using namespace std::chrono_literals;

namespace {

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

void pumpFor(int ms) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

bool finished(ProcessorBase& processor) {
    return processor.generationStatus() != GenerationStatus::Generating;
}

/// The answer an AI would give if it wrote exactly the pattern the algorithmic generator makes for `seed`.
std::string validAnswer(ProcessorBase& processor, uint64_t seed = 5, uint32_t bars = 4) {
    const auto* style = processor.styles().find("peak_time");
    REQUIRE(style != nullptr);
    mm::core::GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    const auto result = mm::core::generatePattern(*style, request);
    REQUIRE(result.success);
    return mm::ai::patternToSchemaJson(result.pattern, style, false)->json;
}

std::shared_ptr<mm::ai::MockProvider> mock() {
    return std::make_shared<mm::ai::MockProvider>();
}

AiBackendSettings backendWith(std::shared_ptr<mm::ai::MockProvider> provider) {
    AiBackendSettings settings;
    settings.provider = std::move(provider);
    settings.model = "mock-model";
    return settings;
}

bool slotEmpty(ProcessorBase& processor, size_t index = 0) {
    return processor.slotsSnapshot().isEmpty(index);
}

juce::Component* findById(juce::Component& parent, const juce::String& id) {
    for (auto* component : parent.getChildren()) {
        if (component->getComponentID() == id) {
            return component;
        }
        if (auto* deeper = findById(*component, id)) {
            return deeper;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("without a provider generate stays offline and never asks the AI", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto provider = mock();
    InstrumentProcessor processor("Test");
    CHECK_FALSE(AiBackend::instance().active());
    processor.generate();
    CHECK_FALSE(processor.generatingWithAi());
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Done);
    CHECK_FALSE(processor.lastReport().usedAi);
    CHECK(provider->requestCount() == 0);
}

TEST_CASE("with a provider generate asks the AI with the prompt and puts the answer in the slot", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    provider->enqueueText(validAnswer(processor));
    ScopedAiBackend backend(backendWith(provider));

    auto settings = processor.instanceSettings();
    settings.generation.prompt = "dark rolling bass with a short call";
    processor.setInstanceSettings(settings);
    processor.generate();
    CHECK(processor.generatingWithAi());
    REQUIRE(pumpUntil([&] { return finished(processor); }));

    CHECK(processor.generationStatus() == GenerationStatus::Done);
    CHECK(processor.lastReport().usedAi);
    REQUIRE(provider->requestCount() == 1);
    const auto request = provider->requests().front();
    CHECK(request.model == "mock-model");
    CHECK(request.userPrompt.find("dark rolling bass with a short call") != std::string::npos);
    const auto bank = processor.slotsSnapshot();
    REQUIRE(bank.slot(0)->pattern.has_value());
    CHECK(bank.slot(0)->pattern->info.providerId == "mock");
    CHECK(bank.slot(0)->history.size() == 1);
}

TEST_CASE("a provider error leaves the slot alone and says why", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::AuthFailed, "bad key", 401);
    ScopedAiBackend backend(backendWith(provider));

    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::AiFailed);
    CHECK(processor.lastReport().status == mm::ai::AiStatus::AuthFailed);
    CHECK(processor.lastReport().httpStatus == 401);
    CHECK(slotEmpty(processor));
    CHECK(provider->requestCount() == 1); // an error is not repaired
}

TEST_CASE("an unusable answer is repaired once, then the request fails", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    provider->enqueueText("this is not json");
    provider->enqueueText("{}");
    ScopedAiBackend backend(backendWith(provider));

    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::AiFailed);
    CHECK(processor.lastReport().outcome == mm::ai::AiOutcome::Invalid);
    CHECK(provider->requestCount() == 2);
    CHECK(slotEmpty(processor));
}

TEST_CASE("after a failure the same request can be sent again", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::NetworkError, "offline");
    provider->enqueueText(validAnswer(processor));
    ScopedAiBackend backend(backendWith(provider));

    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::AiFailed);

    processor.retryGeneration();
    CHECK(processor.generationStatus() == GenerationStatus::Generating);
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Done);
    CHECK(provider->requestCount() == 2);
    CHECK_FALSE(slotEmpty(processor));
}

TEST_CASE("after a failure the same request can run offline, with the same seed", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.seed = 777;
    processor.setInstanceSettings(settings);
    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::Timeout, "slow");
    ScopedAiBackend backend(backendWith(provider));

    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::AiFailed);
    processor.generateOffline();
    CHECK_FALSE(processor.generatingWithAi());
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Done);
    CHECK_FALSE(processor.lastReport().usedAi);
    CHECK(provider->requestCount() == 1);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->info.seed == 777);
}

TEST_CASE("with automatic offline the offline generator takes over after a failure", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    processor.setAutoOffline(true);
    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::ServerError, "boom", 503);
    ScopedAiBackend backend(backendWith(provider));

    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    // The first job ended with an error and the offline job follows at once.
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::Done; }));
    CHECK_FALSE(slotEmpty(processor));
    CHECK(provider->requestCount() == 1);
}

TEST_CASE("cancel ends a running AI request and nothing arrives", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    provider->enqueueText(validAnswer(processor), 400ms);
    ScopedAiBackend backend(backendWith(provider));

    processor.generate();
    REQUIRE(processor.generatingWithAi());
    pumpFor(50);
    processor.cancelGeneration();
    CHECK(processor.generationStatus() == GenerationStatus::GenerationCancelled);
    CHECK_FALSE(processor.generatingWithAi());
    pumpFor(1000); // the answer would have come by now
    CHECK(processor.generationStatus() == GenerationStatus::GenerationCancelled);
    CHECK(slotEmpty(processor));
}

TEST_CASE("the AI writes only the unlocked voices of the slot", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.lengthBars = 2;
    processor.setInstanceSettings(settings);
    processor.generate(); // offline: no backend yet
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Done);
    const auto first = *processor.slotsSnapshot().slot(0)->pattern;
    REQUIRE(processor.setVoiceLocked(0, 0, true));

    auto provider = mock();
    provider->enqueueText(validAnswer(processor, 9, 2));
    ScopedAiBackend backend(backendWith(provider));
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::DoneLocked);
    REQUIRE(provider->requestCount() == 1);
    CHECK(provider->requests().front().userPrompt.find("locked") != std::string::npos);
    const auto next = *processor.slotsSnapshot().slot(0)->pattern;
    CHECK(next.voices[0].notes == first.voices[0].notes);
    CHECK_FALSE(next.voices[1].notes == first.voices[1].notes);
}

TEST_CASE("cancel does nothing when no request is running", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    processor.cancelGeneration();
    CHECK(processor.generationStatus() == GenerationStatus::Idle);
}

TEST_CASE("retry and offline without an earlier request do nothing", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    processor.retryGeneration();
    processor.generateOffline();
    CHECK(processor.generationStatus() == GenerationStatus::Idle);
}

TEST_CASE("destroying a processor during an AI request is safe and quick", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto provider = mock();
    {
        InstrumentProcessor processor("Test");
        provider->enqueueText(validAnswer(processor), 30000ms);
        ScopedAiBackend backend(backendWith(provider));
        processor.generate();
        pumpFor(50);
        CHECK(processor.generatingWithAi());
    }
    pumpFor(100);
    SUCCEED();
}

TEST_CASE("a job keeps the provider it was requested with", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto first = mock();
    first->enqueueText(validAnswer(processor), 200ms);
    auto second = mock();
    {
        ScopedAiBackend backend(backendWith(first));
        processor.generate();
    }
    // The backend is gone again, but the running request still goes to its provider.
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Done);
    CHECK(first->requestCount() == 1);
    CHECK(second->requestCount() == 0);
}

TEST_CASE("the prompt is saved with the state and cut to a sane length on load", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor source("Test");
    auto settings = source.instanceSettings();
    settings.generation.prompt = "Melodie mit Quinten – leicht süß";
    source.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.instanceSettings().generation.prompt == settings.generation.prompt);

    settings.generation.prompt = std::string(mm::core::kMaxPromptBytes - 1, 'a') + "ä" + std::string(100, 'b');
    source.setInstanceSettings(settings);
    const auto cut = source.instanceSettings().generation.prompt;
    CHECK(cut.size() <= mm::core::kMaxPromptBytes);
    CHECK(cut.size() == mm::core::kMaxPromptBytes - 1); // the two bytes of "ä" do not fit: the cut is before them
}

TEST_CASE("the embedded prompt templates are complete for every style", "[ai-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto& templates = embeddedPrompts();
    CHECK_FALSE(templates.system.empty());
    CHECK_FALSE(templates.generate.empty());
    CHECK_FALSE(templates.refine.empty());
    for (const auto& style : processor.styles().profiles()) {
        INFO(style.id);
        CHECK(templates.styles.count(style.id) == 1);
    }
}

TEST_CASE("the editor shows the prompt field, cancel while the AI works and the error bar after a failure",
          "[ai-plugin][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* prompt = dynamic_cast<juce::TextEditor*>(findById(*editor, "prompt"));
    auto* generate = dynamic_cast<juce::TextButton*>(findById(*editor, "generate"));
    auto* retry = dynamic_cast<juce::TextButton*>(findById(*editor, "retry"));
    auto* offline = dynamic_cast<juce::TextButton*>(findById(*editor, "offline"));
    REQUIRE(prompt != nullptr);
    REQUIRE(generate != nullptr);
    REQUIRE(retry != nullptr);
    REQUIRE(offline != nullptr);
    CHECK(prompt->isVisible());
    CHECK_FALSE(retry->isVisible());

    prompt->setText("fast and dark", true);
    CHECK(pumpUntil([&] { return processor.instanceSettings().generation.prompt == "fast and dark"; })); // async

    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::RateLimited, "slow down", 429);
    provider->enqueueText(validAnswer(processor), 30000ms);
    ScopedAiBackend backend(backendWith(provider));
    const auto generateText = generate->getButtonText();
    generate->onClick();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::AiFailed);
    REQUIRE(pumpUntil([&] { return retry->isVisible(); }));
    CHECK(offline->isVisible());

    retry->onClick(); // the second answer takes 30 s: cancel it
    CHECK(processor.generatingWithAi());
    CHECK(generate->getButtonText() != generateText); // "Cancel"
    generate->onClick();
    CHECK(processor.generationStatus() == GenerationStatus::GenerationCancelled);
    pumpFor(100);
    CHECK_FALSE(retry->isVisible());
    CHECK(generate->getButtonText() == generateText);
}
