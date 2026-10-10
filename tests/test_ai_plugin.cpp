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

namespace {

/// Makes an offline pattern in slot 1 and returns it.
mm::core::Pattern fillSlot(ProcessorBase& processor, uint32_t bars = 2) {
    auto settings = processor.instanceSettings();
    settings.generation.lengthBars = bars;
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Done);
    return *processor.slotsSnapshot().slot(0)->pattern;
}

/// The answer of an AI that returns `pattern` with every velocity of voice `voice` set to `velocity`.
std::string answerWithVelocity(ProcessorBase& processor, mm::core::Pattern pattern, size_t voice, uint8_t velocity) {
    for (auto& note : pattern.voices[voice].notes) {
        note.velocity = velocity;
    }
    return mm::ai::patternToSchemaJson(pattern, processor.styles().find("peak_time"), true)->json;
}

} // namespace

TEST_CASE("refine asks the AI with the pattern and the wish, and the result is a history entry and an undo step",
          "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor);
    auto provider = mock();
    provider->enqueueText(answerWithVelocity(processor, first, 1, 37));
    ScopedAiBackend backend(backendWith(provider));

    REQUIRE(processor.refine("softer melody", 1));
    CHECK(processor.generationStatus() == GenerationStatus::Generating);
    CHECK(processor.generatingWithAi());
    CHECK(processor.lastJobIsRefine());
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Refined);

    REQUIRE(provider->requestCount() == 1);
    const auto prompt = provider->requests().front().userPrompt;
    CHECK(prompt.find("softer melody") != std::string::npos);
    CHECK(prompt.find("only the melody voice") != std::string::npos);
    CHECK(prompt.find("\"id\":") != std::string::npos);

    const auto bank = processor.slotsSnapshot();
    const auto& refined = *bank.slot(0)->pattern;
    CHECK(refined.info.source == "refine");
    CHECK(refined.refineHistory == std::vector<std::string>{"softer melody"});
    CHECK(refined.voices[0] == first.voices[0]); // the bass was not in the scope
    CHECK(refined.voices[1].notes.front().velocity == 37);
    CHECK(bank.slot(0)->history.size() == 2);

    REQUIRE(processor.canUndo());
    CHECK(processor.undo());
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices == first.voices);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->refineHistory.empty());
}

TEST_CASE("a refinement is made in the style of the pattern, not the style that is chosen now", "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor);
    REQUIRE(first.styleId == "peak_time");
    auto settings = processor.instanceSettings();
    settings.generation.styleId = "hard_industrial";
    processor.setInstanceSettings(settings);
    auto provider = mock();
    provider->enqueueText(answerWithVelocity(processor, first, 1, 60));
    ScopedAiBackend backend(backendWith(provider));
    REQUIRE(processor.refine("quieter"));
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(provider->requestCount() == 1);
    const auto& templates = embeddedPrompts();
    const auto system = provider->requests().front().systemPrompt;
    REQUIRE_FALSE(templates.styles.at("peak_time").empty());
    CHECK(system.find(templates.styles.at("peak_time").substr(0, 40)) != std::string::npos);
    CHECK(system.find(templates.styles.at("hard_industrial").substr(0, 40)) == std::string::npos);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->styleId == "peak_time");
}

TEST_CASE("the second refinement sends the first one as context, and generating anew starts without it",
          "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor);
    auto provider = mock();
    provider->enqueueText(answerWithVelocity(processor, first, 1, 60));
    ScopedAiBackend backend(backendWith(provider));
    REQUIRE(processor.refine("quieter"));
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    const auto second = *processor.slotsSnapshot().slot(0)->pattern;
    provider->enqueueText(answerWithVelocity(processor, second, 1, 50));
    REQUIRE(processor.refine("even quieter"));
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(provider->requestCount() == 2);
    CHECK(provider->requests()[1].userPrompt.find("quieter>>>") != std::string::npos);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->refineHistory.size() == 2);

    provider->enqueueText(validAnswer(processor, 9, 2));
    processor.generate(); // a new generation: the context starts over
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Done);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->refineHistory.empty());
}

TEST_CASE("refine says why it cannot start and starts no request", "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto provider = mock();
    ScopedAiBackend backend(backendWith(provider));

    CHECK_FALSE(processor.refine("anything")); // the slot is empty
    CHECK(processor.generationStatus() == GenerationStatus::NothingToRefine);
    {
        ScopedAiBackend offline(AiBackendSettings{}); // the slot is filled without the AI
        fillSlot(processor);
    }
    CHECK_FALSE(processor.refine("   "));
    CHECK(processor.generationStatus() == GenerationStatus::NothingToRefine);
    CHECK_FALSE(processor.refine("quieter", 7)); // no such voice
    CHECK(processor.generationStatus() == GenerationStatus::NothingToRefine);

    REQUIRE(processor.setVoiceLocked(0, 0, true));
    CHECK_FALSE(processor.refine("quieter", 0));
    CHECK(processor.generationStatus() == GenerationStatus::RefineAllLocked);
    REQUIRE(processor.setVoiceLocked(0, 1, true));
    CHECK_FALSE(processor.refine("quieter"));
    CHECK(processor.generationStatus() == GenerationStatus::RefineAllLocked);
    CHECK(provider->requestCount() == 0);

    ScopedAiBackend none(AiBackendSettings{});
    CHECK_FALSE(processor.refine("quieter"));
    CHECK(processor.generationStatus() == GenerationStatus::RefineNeedsAi);

    InstrumentProcessor voice("Voice");
    auto settings = voice.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    voice.setInstanceSettings(settings);
    CHECK_FALSE(voice.refine("quieter"));
    CHECK(voice.generationStatus() == GenerationStatus::UseHub);
}

TEST_CASE("a failed refinement keeps the slot, can be retried and never falls back to offline", "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor);
    processor.setAutoOffline(true);
    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::ServerError, "down", 503);
    ScopedAiBackend backend(backendWith(provider));

    REQUIRE(processor.refine("quieter"));
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::AiFailed); // auto-offline has nothing to run
    CHECK(*processor.slotsSnapshot().slot(0)->pattern == first);

    processor.generateOffline(); // there is no offline refinement
    pumpFor(100);
    CHECK(processor.generationStatus() == GenerationStatus::AiFailed);
    CHECK(*processor.slotsSnapshot().slot(0)->pattern == first);

    provider->enqueueText(answerWithVelocity(processor, first, 1, 44));
    processor.retryGeneration();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::Refined);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[1].notes.front().velocity == 44);
}

TEST_CASE("cancel ends a running refinement and nothing arrives", "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor);
    auto provider = mock();
    provider->enqueueText(answerWithVelocity(processor, first, 1, 44), 400ms);
    ScopedAiBackend backend(backendWith(provider));
    REQUIRE(processor.refine("quieter"));
    pumpFor(50);
    processor.cancelGeneration();
    pumpFor(800);
    CHECK(processor.generationStatus() == GenerationStatus::GenerationCancelled);
    CHECK(*processor.slotsSnapshot().slot(0)->pattern == first);
}

TEST_CASE("a refinement for a pattern that was replaced meanwhile is dropped", "[ai-plugin][refine]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor, 2);
    auto provider = mock();
    provider->enqueueText(answerWithVelocity(processor, first, 1, 44), 300ms);
    ScopedAiBackend backend(backendWith(provider));
    REQUIRE(processor.refine("quieter"));
    pumpFor(30);
    // while the AI works, the slot gets a pattern of another length
    processor.editSlots(
        [](mm::core::SlotBank& bank) { bank.setResult(0, mm::core::makeEmptyPattern(4, "peak_time")); });
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    CHECK(processor.generationStatus() == GenerationStatus::NoResult);
    CHECK(processor.slotsSnapshot().slot(0)->pattern->lengthBars == 4);
}

TEST_CASE("the editor refines with its field, keeps the wish after a failure and clears it after success",
          "[ai-plugin][refine][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    const auto first = fillSlot(processor, 8);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* text = dynamic_cast<juce::TextEditor*>(findById(*editor, "refine_text"));
    auto* scope = dynamic_cast<juce::ComboBox*>(findById(*editor, "refine_scope"));
    auto* button = dynamic_cast<juce::TextButton*>(findById(*editor, "refine"));
    auto* offline = dynamic_cast<juce::TextButton*>(findById(*editor, "offline"));
    REQUIRE(text != nullptr);
    REQUIRE(scope != nullptr);
    REQUIRE(button != nullptr);
    CHECK(text->isVisible());
    CHECK(scope->isVisible());
    CHECK(button->isVisible());

    // the scope lists the whole pattern, both voices and the phrases of the slot
    REQUIRE(pumpUntil([&] { return scope->getNumItems() > 1; }));
    CHECK(scope->indexOfItemId(1) >= 0);
    CHECK(scope->indexOfItemId(100) >= 0);
    CHECK(scope->indexOfItemId(101) >= 0);
    const auto targets = processor.refineTargets();
    if (targets.phrases.size() > 1) {
        CHECK(scope->indexOfItemId(200) >= 0);
    }
    scope->setSelectedId(101, juce::sendNotification);
    // two phrases make the phrases selectable
    processor.editSlots([](mm::core::SlotBank& bank) {
        auto pattern = *bank.slot(0)->pattern;
        pattern.phrases.clear();
        for (const uint32_t start : {0u, 4u}) {
            mm::core::Phrase phrase;
            phrase.startBar = start;
            phrase.lengthBars = 4;
            phrase.role = start == 0 ? mm::core::PhraseRole::Main : mm::core::PhraseRole::Variation;
            pattern.phrases.push_back(phrase);
        }
        bank.setResult(0, pattern);
    });
    REQUIRE(pumpUntil([&] { return scope->indexOfItemId(201) >= 0; }));
    scope->setSelectedId(201, juce::sendNotification);
    const auto firstPattern = *processor.slotsSnapshot().slot(0)->pattern;

    auto provider = mock();
    provider->enqueueError(mm::ai::AiStatus::NetworkError, "no route");
    ScopedAiBackend backend(backendWith(provider));
    text->setText("make it quieter", juce::dontSendNotification);
    button->onClick();
    CHECK_FALSE(button->isEnabled()); // while the request runs
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(pumpUntil([&] { return button->isEnabled(); }));
    CHECK(processor.generationStatus() == GenerationStatus::AiFailed);
    CHECK(text->getText() == "make it quieter"); // kept for the next try
    CHECK_FALSE(offline->isVisible());           // nothing to run offline for a refinement
    REQUIRE(provider->requestCount() == 1);
    CHECK(provider->requests().front().userPrompt.find("only the bars 5 to 8") == std::string::npos);
    CHECK(provider->requests().front().userPrompt.find("only the bars 4 to 7") != std::string::npos);

    provider->enqueueText(answerWithVelocity(processor, firstPattern, 1, 44));
    text->onReturnKey(); // Return in the field does the same as the button
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(pumpUntil([&] { return text->getText().isEmpty(); }));
    CHECK(processor.generationStatus() == GenerationStatus::Refined);

    // the status stays "Refined" until the next action: a wish typed now must not vanish
    text->setText("next wish", juce::dontSendNotification);
    pumpFor(300);
    CHECK(text->getText() == "next wish");
}

TEST_CASE("the voice layout has no refine controls", "[ai-plugin][refine][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Voice");
    auto settings = processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    REQUIRE(editor != nullptr);
    auto* button = findById(*editor, "refine");
    REQUIRE(button != nullptr);
    CHECK(pumpUntil([&] { return !button->isVisible(); }));
}

namespace {

/// A valid but poor answer for 2 bars: the bass only on the kick steps with different notes and no repetition, the
/// melody on every 16th with notes drawn without a motif, all of one length. It scores under the minimum of the style
/// (50 for Peak Time).
std::string noisyAnswer() {
    uint32_t state = 12345;
    const auto next = [&state](uint32_t range) {
        state = state * 1664525u + 1013904223u;
        return (state >> 16) % range;
    };
    std::string voices;
    for (const char* role : {"bass", "melody"}) {
        const bool bass = std::string(role) == "bass";
        std::string notes;
        for (int step = 0; step < 32; step += bass ? 4 : 1) {
            notes += std::string(notes.empty() ? "" : ",") + R"({"step":)" + std::to_string(step) + R"(,"degree":)" +
                     std::to_string(1 + next(7)) + R"(,"alt":)" + std::to_string(int(next(3)) - 1) + R"(,"octave":)" +
                     std::to_string(bass ? 0 : int(next(3)) - 1) + R"(,"len":1,"vel":127})";
        }
        voices += std::string(voices.empty() ? "" : ",") + R"({"role":")" + role + R"(","notes":[)" + notes + "]}";
    }
    return R"({"schema_version":1,"context":{"root":"A","scale":"natural_minor","progression":["i"]},"voices":[)" +
           voices + "]}";
}

juce::Label* statusLabelOf(juce::AudioProcessorEditor& editor) {
    return dynamic_cast<juce::Label*>(findById(editor, "status"));
}

} // namespace

TEST_CASE("an AI result under the minimum of the style is delivered and the status shows its rating",
          "[ai-plugin][quality][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.lengthBars = 2;
    processor.setInstanceSettings(settings);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* label = statusLabelOf(*editor);
    REQUIRE(label != nullptr);

    auto provider = mock();
    provider->enqueueText(noisyAnswer());
    ScopedAiBackend backend(backendWith(provider));
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Done);
    REQUIRE(processor.lastReport().belowMinScore);
    CHECK_FALSE(slotEmpty(processor)); // shown and kept, not refused
    CHECK(processor.lastReport().score == processor.slotsSnapshot().slot(0)->pattern->qualityScore);
    const auto firstScore = processor.lastReport().score;
    const auto score = std::to_string(firstScore);
    REQUIRE(pumpUntil([&] { return label->getText().contains("Qualit"); }));
    CHECK(label->getText().contains(score));

    // the same for a refinement
    const auto first = *processor.slotsSnapshot().slot(0)->pattern;
    provider->enqueueText(noisyAnswer());
    REQUIRE(processor.refine("noisier"));
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Refined);
    REQUIRE(processor.lastReport().belowMinScore);
    REQUIRE(pumpUntil([&] { return label->getText().contains("Qualit"); }));
    CHECK(label->getText().contains(std::to_string(processor.lastReport().score)));
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices != first.voices);
    CHECK(processor.lastReport().score == processor.slotsSnapshot().slot(0)->pattern->qualityScore);
}

TEST_CASE("a good AI result shows the plain done message", "[ai-plugin][quality][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* label = statusLabelOf(*editor);
    REQUIRE(label != nullptr);
    auto provider = mock();
    provider->enqueueText(validAnswer(processor));
    ScopedAiBackend backend(backendWith(provider));
    processor.generate();
    REQUIRE(pumpUntil([&] { return finished(processor); }));
    REQUIRE(processor.generationStatus() == GenerationStatus::Done);
    if (!processor.lastReport().belowMinScore) {
        REQUIRE(pumpUntil([&] { return !label->getText().isEmpty(); }));
        CHECK_FALSE(label->getText().contains("Qualit"));
    }
}

TEST_CASE("the creativity slider reaches the temperature and the prompt, for generating and refining",
          "[ai-plugin][creativity]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    for (const int creativity : {90, 10}) {
        auto settings = processor.instanceSettings();
        settings.generation.creativityPct = creativity;
        settings.generation.lengthBars = 2;
        processor.setInstanceSettings(settings);
        auto provider = mock();
        provider->enqueueText(validAnswer(processor, 5, 2));
        ScopedAiBackend backend(backendWith(provider));
        processor.generate();
        REQUIRE(pumpUntil([&] { return finished(processor); }));
        REQUIRE(processor.generationStatus() == GenerationStatus::Done);
        const auto pattern = *processor.slotsSnapshot().slot(0)->pattern;
        provider->enqueueText(mm::ai::patternToSchemaJson(pattern, processor.styles().find("peak_time"), true)->json);
        REQUIRE(processor.refine("anything"));
        REQUIRE(pumpUntil([&] { return finished(processor); }));
        REQUIRE(provider->requestCount() == 2);
        for (const auto& request : provider->requests()) {
            CHECK(request.temperature == creativity / 100.0);
            CHECK(request.userPrompt.find(std::to_string(creativity) + " percent") != std::string::npos);
            CHECK(request.userPrompt.find(creativity > 50 ? "experimental" : "stay close to the style") !=
                  std::string::npos);
        }
    }
}
