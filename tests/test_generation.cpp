#include "core/PatternGenerator.h"
#include "core/PlaybackRender.h"
#include "plugin/GenerationService.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <thread>
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
    settings.generation = {"melodic_techno", 8, 70, 20, 2, "dorian", 12345};
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

// ---- hub and voices (SPEC 6.5, D-141) ----

namespace {

using mm::core::GroupStatus;
using mm::core::InstanceRole;

void setRole(mm::plugin::ProcessorBase& processor, InstanceRole role, int voice = 1) {
    auto settings = processor.instanceSettings();
    settings.role = role;
    settings.outputVoice = voice;
    processor.setInstanceSettings(settings);
}

mm::core::Pattern groupPattern(const mm::plugin::ProcessorBase& processor, const std::string& style, uint64_t seed) {
    mm::core::GenerationRequest request;
    request.seed = seed;
    return mm::core::generateCandidate(*processor.styles().find(style), request, seed);
}

int firstNotePitch(const std::vector<Played>& notes) {
    return notes.empty() ? -1 : notes.front().pitch;
}

} // namespace

TEST_CASE("instances join the registry and leave it again", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto& registry = mm::plugin::processGroup();
    REQUIRE(registry.memberCount() == 0);
    {
        mm::plugin::InstrumentProcessor a("A");
        mm::plugin::MidiFxProcessor b("B");
        CHECK(registry.memberCount() == 2);
        CHECK(a.groupStatus() == GroupStatus::Solo);
    }
    CHECK(registry.memberCount() == 0);
}

TEST_CASE("a voice plays its voice of the slots that the hub holds", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor hub("Hub");
    mm::plugin::InstrumentProcessor voice("Voice");
    setRole(hub, InstanceRole::Hub, 1);
    setRole(voice, InstanceRole::Voice, 2);
    CHECK(hub.groupStatus() == GroupStatus::Hub);
    CHECK(hub.groupVoices() == 1);
    CHECK(voice.groupStatus() == GroupStatus::VoiceConnected);

    const auto pattern = groupPattern(hub, "melodic_techno", 9);
    hub.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });

    const auto hubBank = hub.slotsSnapshot();
    const auto voiceBank = voice.slotsSnapshot();
    REQUIRE(voiceBank.slot(0)->pattern.has_value());
    CHECK(*voiceBank.slot(0)->pattern == *hubBank.slot(0)->pattern);

    const auto& style = *hub.styles().find("melodic_techno");
    const auto bass = mm::core::renderVoiceForPlayback(*hubBank.slot(0)->pattern, style, 0);
    const auto melody = mm::core::renderVoiceForPlayback(*hubBank.slot(0)->pattern, style, 1);
    REQUIRE_FALSE(bass.notes.empty());
    REQUIRE_FALSE(melody.notes.empty());
    CHECK(firstNotePitch(playBlocks(hub, 400)) == bass.notes.front().pitch);
    CHECK(firstNotePitch(playBlocks(voice, 400)) == melody.notes.front().pitch);
}

TEST_CASE("a change of the hub reaches the voice", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor hub("Hub");
    mm::plugin::InstrumentProcessor voice("Voice");
    setRole(hub, InstanceRole::Hub);
    setRole(voice, InstanceRole::Voice, 2);
    hub.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(0, groupPattern(hub, "peak_time", 1)); });
    hub.editSlots([&](mm::core::SlotBank& bank) {
        bank.setResult(4, groupPattern(hub, "hard_industrial", 2));
        bank.setName(4, "Drop");
    });
    const auto bank = voice.slotsSnapshot();
    CHECK_FALSE(bank.isEmpty(0));
    REQUIRE_FALSE(bank.isEmpty(4));
    CHECK(bank.slot(4)->name == "Drop");
    CHECK(bank.slot(4)->pattern->styleId == "hard_industrial");
}

TEST_CASE("the voice generates nothing itself", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor voice("Voice");
    setRole(voice, InstanceRole::Voice, 2);
    voice.generate();
    CHECK(voice.generationStatus() == mm::plugin::GenerationStatus::UseHub);
    pumpFor(300);
    CHECK(voice.slotsSnapshot().isEmpty(0));
}

TEST_CASE("a second hub is refused and stays solo", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor first("First");
    mm::plugin::InstrumentProcessor second("Second");
    setRole(first, InstanceRole::Hub);
    setRole(second, InstanceRole::Hub);
    CHECK(first.groupStatus() == GroupStatus::Hub);
    CHECK(second.groupStatus() == GroupStatus::HubRefused);
}

TEST_CASE("when the hub is deleted the voice keeps playing and can take over", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor voice("Voice");
    mm::plugin::InstrumentProcessor other("Other");
    int pitchBefore = -1;
    {
        mm::plugin::InstrumentProcessor hub("Hub");
        setRole(hub, InstanceRole::Hub);
        setRole(voice, InstanceRole::Voice, 1);
        setRole(other, InstanceRole::Voice, 1);
        hub.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(0, groupPattern(hub, "peak_time", 4)); });
        pitchBefore = firstNotePitch(playBlocks(voice, 400));
        REQUIRE(pitchBefore > 0);
    }
    CHECK(voice.groupStatus() == GroupStatus::HubOffered); // the oldest voice
    CHECK(other.groupStatus() == GroupStatus::HubMissing);
    CHECK_FALSE(voice.slotsSnapshot().isEmpty(0)); // it still has everything
    CHECK(firstNotePitch(playBlocks(voice, 400)) == pitchBefore);

    other.acceptHubOffer(); // not offered to this one: nothing happens
    CHECK(other.groupStatus() == GroupStatus::HubMissing);
    voice.acceptHubOffer();
    CHECK(voice.groupStatus() == GroupStatus::Hub);
    CHECK(voice.groupVoices() == 1);
    CHECK(other.groupStatus() == GroupStatus::VoiceConnected);
    CHECK_FALSE(other.slotsSnapshot().isEmpty(0)); // adopted from the new hub
}

TEST_CASE("a voice that is loaded before its hub takes the hub's slots when it appears", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::MemoryBlock savedVoice;
    {
        mm::plugin::InstrumentProcessor hub("Hub");
        mm::plugin::InstrumentProcessor voice("Voice");
        setRole(hub, InstanceRole::Hub);
        setRole(voice, InstanceRole::Voice, 2);
        hub.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(0, groupPattern(hub, "peak_time", 1)); });
        voice.getStateInformation(savedVoice);
    }
    mm::plugin::InstrumentProcessor voice("Voice");
    voice.setStateInformation(savedVoice.getData(), static_cast<int>(savedVoice.getSize()));
    CHECK(voice.instanceSettings().role == InstanceRole::Voice);
    CHECK(voice.instanceSettings().outputVoice == 2);
    CHECK_FALSE(voice.slotsSnapshot().isEmpty(0)); // the voice keeps what it saved
    CHECK(voice.groupStatus() == GroupStatus::HubOffered);

    mm::plugin::InstrumentProcessor hub("Hub");
    hub.editSlots([&](mm::core::SlotBank& b) { b.setResult(3, groupPattern(hub, "hard_industrial", 8)); });
    setRole(hub, InstanceRole::Hub);
    CHECK(voice.groupStatus() == GroupStatus::VoiceConnected);
    const auto adopted = voice.slotsSnapshot();
    CHECK(adopted.isEmpty(0)); // the hub's state, not the old one
    CHECK_FALSE(adopted.isEmpty(3));
}

TEST_CASE("a hub that loads its state hands the slots to the voices", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::MemoryBlock savedHub;
    {
        mm::plugin::InstrumentProcessor source("Source");
        setRole(source, InstanceRole::Hub);
        source.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(1, groupPattern(source, "peak_time", 3)); });
        source.getStateInformation(savedHub);
    }
    mm::plugin::InstrumentProcessor voice("Voice");
    setRole(voice, InstanceRole::Voice, 2);
    CHECK(voice.slotsSnapshot().isEmpty(1));
    mm::plugin::InstrumentProcessor hub("Hub");
    hub.setStateInformation(savedHub.getData(), static_cast<int>(savedHub.getSize()));
    CHECK(hub.groupStatus() == GroupStatus::Hub);
    CHECK_FALSE(voice.slotsSnapshot().isEmpty(1));
}

TEST_CASE("the voice does not publish to the group and a hub change does not restart an unchanged voice",
          "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor hub("Hub");
    mm::plugin::InstrumentProcessor voice("Voice");
    setRole(hub, InstanceRole::Hub);
    setRole(voice, InstanceRole::Voice, 2);
    hub.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(0, groupPattern(hub, "peak_time", 1)); });
    const auto version = voice.slotsSnapshot().slot(0)->pattern->version;
    // Changing a setting of the hub hands the same slots on again: the voice has nothing new to play.
    auto settings = hub.instanceSettings();
    settings.generation.energyPct = 70;
    hub.setInstanceSettings(settings);
    CHECK(voice.slotsSnapshot().slot(0)->pattern->version == version);
    CHECK(voice.slotsSnapshot().origin() == hub.slotsSnapshot().origin());
}

TEST_CASE("an instance made and destroyed on another thread joins and leaves on the message thread", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto& registry = mm::plugin::processGroup();
    REQUIRE(registry.memberCount() == 0);
    std::unique_ptr<mm::plugin::InstrumentProcessor> processor;
    std::thread maker([&] { processor = std::make_unique<mm::plugin::InstrumentProcessor>("Off-thread"); });
    maker.join();
    CHECK(registry.memberCount() == 0); // not yet: the join is queued for the message thread
    REQUIRE(pumpUntil([&] { return registry.memberCount() == 1; }));

    std::thread killer([&] { processor.reset(); });
    killer.join();
    REQUIRE(pumpUntil([&] { return registry.memberCount() == 0; }));
}

TEST_CASE("an instance destroyed before its queued join ran leaves no trace in the registry", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto& registry = mm::plugin::processGroup();
    REQUIRE(registry.memberCount() == 0);
    std::thread worker([] { mm::plugin::InstrumentProcessor processor("Short-lived"); });
    worker.join();
    pumpFor(300);
    CHECK(registry.memberCount() == 0);
}

TEST_CASE("the editor shows the group and can be used while the hub goes away", "[group-plugin]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor voice("Voice");
    setRole(voice, InstanceRole::Voice, 2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.createEditor());
    REQUIRE(editor != nullptr);
    {
        mm::plugin::InstrumentProcessor hub("Hub");
        setRole(hub, InstanceRole::Hub);
        pumpFor(300);
    }
    pumpFor(300);
    CHECK(voice.groupStatus() == GroupStatus::HubOffered);
}

TEST_CASE("the key and scale of the settings reach the pattern, auto draws them", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor styleSource("Styles");
    std::vector<Delivered> delivered;
    mm::plugin::GenerationService service(styleSource.styles(),
                                          [&](const auto& job, auto pattern) { delivered.push_back({job, pattern}); });
    for (const auto& [root, scale] :
         {std::pair<int, const char*>{2, "dorian"}, {7, "phrygian"}, {11, "harmonic_minor"}}) {
        auto job = jobFor(5);
        job.settings.root = static_cast<mm::core::PitchClass>(root);
        job.settings.scaleId = scale;
        const size_t before = delivered.size();
        service.request(job);
        REQUIRE(pumpUntil([&] { return delivered.size() == before + 1; }));
        REQUIRE(delivered.back().pattern.has_value());
        CHECK(delivered.back().pattern->context.root == root);
        CHECK(delivered.back().pattern->context.scaleId == scale);
    }

    // Auto: over several seeds the keys differ (drawn per request).
    std::set<int> roots;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        auto job = jobFor(seed);
        job.settings.root.reset();
        job.settings.scaleId.reset();
        const size_t before = delivered.size();
        service.request(job);
        REQUIRE(pumpUntil([&] { return delivered.size() == before + 1; }));
        REQUIRE(delivered.back().pattern.has_value());
        roots.insert(delivered.back().pattern->context.root);
    }
    CHECK(roots.size() > 1);
}

TEST_CASE("a fixed seed makes generate repeat itself, no seed draws a new one", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    auto settings = processor.instanceSettings();
    settings.generation.seed = 4711;
    processor.setInstanceSettings(settings);
    std::vector<mm::core::Pattern> results;
    const auto generateOnce = [&] {
        processor.generate();
        REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
        const auto bank = processor.slotsSnapshot();
        REQUIRE(bank.slot(0)->pattern.has_value());
        results.push_back(*bank.slot(0)->pattern);
        pumpFor(20);
    };
    generateOnce();
    generateOnce();
    CHECK(results[0].info.seed == 4711);
    CHECK(results[0].voices[0].notes.size() == results[1].voices[0].notes.size());
    CHECK(results[0].voices[0].notes.front().pitch == results[1].voices[0].notes.front().pitch);
    CHECK(results[0].info.winnerSeed == results[1].info.winnerSeed);

    settings.generation.seed.reset();
    processor.setInstanceSettings(settings);
    generateOnce();
    generateOnce();
    CHECK(results[2].info.seed != results[3].info.seed);
    CHECK(results[2].info.seed != 4711);
}

TEST_CASE("key, scale and seed are saved, and a state from before them keeps drawing the key", "[generation]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor source("Test");
    CHECK(source.instanceSettings().generation.root == 9); // a new instance starts in A minor
    CHECK(source.instanceSettings().generation.scaleId == "natural_minor");
    CHECK_FALSE(source.instanceSettings().generation.seed.has_value());
    auto settings = source.instanceSettings();
    settings.generation.root = 3;
    settings.generation.scaleId = "locrian";
    settings.generation.seed = 18446744073709551615ULL;
    source.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.instanceSettings().generation == settings.generation);

    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    const auto load = [&](const std::function<void(juce::XmlElement&)>& edit) {
        auto copy = std::make_unique<juce::XmlElement>(*root);
        edit(*copy);
        juce::MemoryBlock block;
        juce::AudioProcessor::copyXmlToBinary(*copy, block);
        mm::plugin::InstrumentProcessor processor("Test");
        processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        return processor.instanceSettings().generation;
    };
    const auto old = load([](juce::XmlElement& element) {
        for (const char* name : {"genRoot", "genScale", "genSeed"}) {
            element.removeAttribute(name);
        }
    });
    CHECK_FALSE(old.root.has_value()); // an older project draws the key as before
    CHECK_FALSE(old.scaleId.has_value());
    CHECK_FALSE(old.seed.has_value());

    const auto odd = load([](juce::XmlElement& element) {
        element.setAttribute("genRoot", "12");
        element.setAttribute("genScale", "bogus");
        element.setAttribute("genSeed", "-3");
    });
    CHECK_FALSE(odd.root.has_value());
    CHECK_FALSE(odd.scaleId.has_value());
    CHECK_FALSE(odd.seed.has_value());

    const auto words = load([](juce::XmlElement& element) {
        element.setAttribute("genRoot", "auto");
        element.setAttribute("genScale", "auto");
        element.setAttribute("genSeed", "random");
    });
    CHECK_FALSE(words.root.has_value());
    CHECK_FALSE(words.scaleId.has_value());
    CHECK_FALSE(words.seed.has_value());

    const auto huge = load([](juce::XmlElement& element) { element.setAttribute("genSeed", "18446744073709551616"); });
    CHECK_FALSE(huge.seed.has_value());
}

TEST_CASE("a generated result is an undo step: undo empties the slot again, redo brings the result back",
          "[generation][edit]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    processor.selectSlot(2);
    processor.generate();
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == mm::plugin::GenerationStatus::Done; }));
    REQUIRE(processor.canUndo());
    const auto generated = *processor.slotsSnapshot().slot(1)->pattern;

    REQUIRE(processor.undo());
    CHECK(processor.slotsSnapshot().isEmpty(1));
    CHECK(processor.slotsSnapshot().slot(1)->history.empty());
    CHECK_FALSE(processor.canUndo());
    REQUIRE(processor.redo());
    const auto again = processor.slotsSnapshot().slot(1)->pattern;
    REQUIRE(again.has_value());
    CHECK(again->voices == generated.voices);
    CHECK(processor.slotsSnapshot().slot(1)->history.size() == 1);
}
