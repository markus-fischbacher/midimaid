#include "core/PlaybackRender.h"
#include "plugin/GenerationService.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace {

/// Runs the message loop until `done` holds or `timeoutMs` passed. Returns whether `done` holds.
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

struct Delivered {
    mm::plugin::GenerationJob job;
    std::optional<mm::core::Pattern> pattern;
};

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

/// Plays `blocks` blocks of 512 samples at 120 BPM from PPQ 0 and returns the note-on samples and pitches.
struct Played {
    long long sample;
    int pitch;
};

std::vector<Played> playBlocks(mm::plugin::ProcessorBase& processor, int blocks) {
    FakePlayHead head;
    processor.setRateAndBufferSizeDetails(48000.0, 512);
    processor.setPlayHead(&head);
    processor.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    std::vector<Played> notes;
    long long clock = 0;
    for (int i = 0; i < blocks; ++i) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(clock) / 48000.0 * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                notes.push_back({clock + metadata.samplePosition, message.getNoteNumber()});
            }
        }
        clock += 512;
    }
    return notes;
}

mm::plugin::GenerationJob jobFor(uint64_t seed, int slot = 0, const std::string& style = "peak_time") {
    mm::plugin::GenerationJob job;
    job.seed = seed;
    job.slot = slot;
    job.settings.styleId = style;
    return job;
}

} // namespace

TEST_CASE("a new instance is silent until something is generated", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(processor.generationStatus() == mm::plugin::GenerationStatus::Idle);
    CHECK(playBlocks(processor, 400).empty());
}

TEST_CASE("the service delivers one pattern for the job, on the message thread", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    std::vector<Delivered> delivered;
    mm::plugin::GenerationService service(styleSource.styles(), [&](const auto& job, auto pattern) {
        CHECK(juce::MessageManager::getInstance()->isThisTheMessageThread());
        delivered.push_back({job, std::move(pattern)});
    });
    service.request(jobFor(42, 6, "melodic_techno"));
    CHECK(service.busy());
    REQUIRE(pumpUntil([&] { return !delivered.empty(); }));
    pumpFor(50);
    REQUIRE(delivered.size() == 1);
    CHECK_FALSE(service.busy());
    CHECK(delivered[0].job.slot == 6);
    REQUIRE(delivered[0].pattern.has_value());
    CHECK(delivered[0].pattern->styleId == "melodic_techno");
    CHECK(delivered[0].pattern->info.seed == 42);
    CHECK(delivered[0].pattern->lengthBars == 4);
}

TEST_CASE("the same job gives the same pattern", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    std::vector<Delivered> delivered;
    mm::plugin::GenerationService service(styleSource.styles(),
                                          [&](const auto& job, auto pattern) { delivered.push_back({job, pattern}); });
    service.request(jobFor(7));
    REQUIRE(pumpUntil([&] { return delivered.size() == 1; }));
    service.request(jobFor(7));
    REQUIRE(pumpUntil([&] { return delivered.size() == 2; }));
    REQUIRE(delivered[0].pattern.has_value());
    CHECK(delivered[0].pattern == delivered[1].pattern);
}

TEST_CASE("a newer request replaces an older one that was not delivered yet", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    std::vector<Delivered> delivered;
    mm::plugin::GenerationService service(styleSource.styles(),
                                          [&](const auto& job, auto pattern) { delivered.push_back({job, pattern}); });
    service.request(jobFor(1));
    service.request(jobFor(2));
    service.request(jobFor(3));
    REQUIRE(pumpUntil([&] { return !service.busy(); }));
    pumpFor(50);
    REQUIRE(delivered.size() == 1);
    CHECK(delivered[0].job.seed == 3);
}

TEST_CASE("destroying the service with a job running delivers nothing and does not hang", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    int calls = 0;
    {
        mm::plugin::GenerationService service(styleSource.styles(), [&](const auto&, auto) { ++calls; });
        service.request(jobFor(1));
        service.request(jobFor(2));
    }
    pumpFor(300); // queued callbacks of the dead service must not call back
    CHECK(calls == 0);
}

TEST_CASE("an unknown style id generates with the fallback style", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    std::vector<Delivered> delivered;
    mm::plugin::GenerationService service(styleSource.styles(),
                                          [&](const auto& job, auto pattern) { delivered.push_back({job, pattern}); });
    service.request(jobFor(5, 0, "no_such_style"));
    REQUIRE(pumpUntil([&] { return !delivered.empty(); }));
    REQUIRE(delivered[0].pattern.has_value());
    CHECK(delivered[0].pattern->styleId == "peak_time");
}

TEST_CASE("generate puts the result in the selected slot and it plays from the next bar line", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    processor.selectSlot(3);
    processor.generate();
    CHECK(processor.generationStatus() == mm::plugin::GenerationStatus::Generating);
    REQUIRE(pumpUntil([&] { return processor.generationStatus() != mm::plugin::GenerationStatus::Generating; }));
    CHECK(processor.generationStatus() == mm::plugin::GenerationStatus::Done);

    const auto bank = processor.slotsSnapshot();
    REQUIRE(bank.slot(2)->pattern.has_value());
    CHECK(bank.isEmpty(0));
    CHECK(bank.slot(2)->pattern->styleId == "peak_time");
    CHECK(bank.slot(2)->history.size() == 1);

    const auto rendered =
        mm::core::renderVoiceForPlayback(*bank.slot(2)->pattern, *processor.styles().find("peak_time"), 0);
    REQUIRE_FALSE(rendered.notes.empty());
    const auto notes = playBlocks(processor, 400); // from PPQ 0: the start plays the selected slot at once
    REQUIRE_FALSE(notes.empty());
    const long long expected = std::llround(rendered.notes.front().startTick / 960.0 * 24000.0);
    CHECK(std::abs(notes.front().sample - expected) <= 1);
    CHECK(notes.front().pitch == rendered.notes.front().pitch);
    CHECK(processor.activePatternVersion() == bank.slot(2)->pattern->version);
}

TEST_CASE("the result goes to the slot that was selected when it was requested", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    processor.selectSlot(3);
    processor.generate();
    processor.selectSlot(5);
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
    const auto bank = processor.slotsSnapshot();
    CHECK_FALSE(bank.isEmpty(2));
    CHECK(bank.isEmpty(4));
}

TEST_CASE("generate uses the settings of the instance", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.styleId = "hard_industrial";
    settings.generation.lengthBars = 2;
    settings.generation.energyPct = 80;
    settings.generation.creativityPct = 10;
    processor.setInstanceSettings(settings);
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
    const auto bank = processor.slotsSnapshot();
    REQUIRE(bank.slot(0)->pattern.has_value());
    CHECK(bank.slot(0)->pattern->styleId == "hard_industrial");
    CHECK(bank.slot(0)->pattern->lengthBars == 2);
    CHECK(bank.slot(0)->pattern->info.energyPct == 80);
    CHECK(bank.slot(0)->pattern->info.creativityPct == 10);
}

TEST_CASE("a result that arrives during an offline render waits until the host is real-time again", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    processor.setNonRealtime(true);
    processor.generate();
    pumpFor(1000); // the job is long done; the result must not have been applied
    CHECK(processor.slotsSnapshot().isEmpty(0));
    CHECK(processor.generationStatus() == mm::plugin::GenerationStatus::Generating);

    processor.setNonRealtime(false);
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
    CHECK_FALSE(processor.slotsSnapshot().isEmpty(0));
}

TEST_CASE("a new request drops a parked result", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    processor.setNonRealtime(true);
    processor.generate();
    pumpFor(1000);
    processor.setNonRealtime(false);
    processor.generate(); // before the timer applied the parked one
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
    pumpFor(600);
    const auto bank = processor.slotsSnapshot();
    REQUIRE(bank.slot(0)->pattern.has_value());
    CHECK(bank.slot(0)->history.size() == 1); // only the second result
}

TEST_CASE("destroying a processor while it generates is safe", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    {
        mm::plugin::InstrumentProcessor processor("Test");
        processor.generate();
    }
    pumpFor(300);
    SUCCEED();
}

TEST_CASE("the generation settings are saved, and invalid ones are made valid on load", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor source("Test");
    auto settings = source.instanceSettings();
    settings.generation = {"melodic_techno", 8, 70, 20};
    source.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.instanceSettings().generation == settings.generation);

    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("genBars", 3);
    root->setAttribute("genEnergy", 500);
    root->setAttribute("genCreativity", -4);
    juce::MemoryBlock odd;
    juce::AudioProcessor::copyXmlToBinary(*root, odd);
    mm::plugin::InstrumentProcessor other("Test");
    other.setStateInformation(odd.getData(), static_cast<int>(odd.getSize()));
    const auto loaded = other.instanceSettings().generation;
    CHECK(loaded.lengthBars == 4);
    CHECK(loaded.energyPct == 100);
    CHECK(loaded.creativityPct == 0);
    CHECK(loaded.styleId == "melodic_techno");
}

TEST_CASE("a state without generation settings loads the defaults", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor source("Test");
    juce::MemoryBlock saved;
    source.getStateInformation(saved);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    for (const char* name : {"genStyle", "genBars", "genEnergy", "genCreativity"}) {
        root->removeAttribute(name);
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*root, old);
    mm::plugin::InstrumentProcessor target("Test");
    auto changed = target.instanceSettings();
    changed.generation.styleId = "hard_industrial";
    target.setInstanceSettings(changed);
    target.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(target.instanceSettings().generation == mm::core::GenerationSettings{});
}

TEST_CASE("the editor can be created and destroyed while a generation runs", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        processor.generate();
        pumpFor(300); // the editor's timer reads the status
    }
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
}

TEST_CASE("a finished job whose delivery is still queued does not call back after the service is gone",
          "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    int calls = 0;
    {
        mm::plugin::GenerationService service(styleSource.styles(), [&](const auto&, auto) { ++calls; });
        service.request(jobFor(1));
        juce::Thread::sleep(1500); // the worker is done and has queued the delivery; the loop has not run
    }
    pumpFor(300);
    CHECK(calls == 0);
}
