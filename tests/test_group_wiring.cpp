#include "core/PatternGenerator.h"
#include "engine/GroupChannel.h"
#include "plugin/EditorParts.h"
#include "plugin/GroupText.h"
#include "plugin/ProcessorBase.h"

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <thread>
#include <vector>

// Hub and voices as plugin instances: the planned slot changes of D-142 wired into processBlock (D-143).

namespace {

using mm::core::GroupStatus;
using mm::core::InstanceRole;
using mm::core::SlotFollow;

constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 480; // 0.02 PPQ at 120 BPM: bar lines fall on block boundaries
constexpr double kSamplesPerPpq = 24000.0;

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

struct NoteOn {
    long long sample;
    int pitch;
};

/// One plugin instance with its own transport: block by block like a host would drive it.
class Instance {
public:
    explicit Instance(const char* name = "Test") : processor(name), audio_(2, kBlock) {
        processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
        processor.setPlayHead(&head_);
        processor.prepareToPlay(kSampleRate, kBlock);
    }

    void setRole(InstanceRole role, SlotFollow follow = SlotFollow::Hub, int voice = 1) {
        auto settings = processor.instanceSettings();
        settings.role = role;
        settings.slotFollow = follow;
        settings.outputVoice = voice;
        processor.setInstanceSettings(settings);
    }
    void setSlotParameter(int slot) {
        auto* parameter = processor.parameters().getParameter("slot");
        REQUIRE(parameter != nullptr);
        parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(slot)));
    }
    void setMute(int voice, bool on) {
        auto* parameter = processor.parameters().getParameter("mute_" + juce::String(voice));
        REQUIRE(parameter != nullptr);
        parameter->setValueNotifyingHost(on ? 1.0f : 0.0f);
    }
    void setPpq(double ppq) { ppq_ = ppq; }
    void setPlaying(bool playing) { playing_ = playing; }
    double ppq() const { return ppq_; }
    const std::vector<NoteOn>& notes() const { return notes_; }

    void step() {
        head_.info.setIsPlaying(playing_);
        head_.info.setBpm(120.0);
        head_.info.setPpqPosition(ppq_);
        audio_.clear();
        midi_.clear();
        processor.processBlock(audio_, midi_);
        for (const auto metadata : midi_) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                notes_.push_back(
                    {std::llround(ppq_ * kSamplesPerPpq) + metadata.samplePosition, message.getNoteNumber()});
            }
        }
        if (playing_) {
            ppq_ += kBlock / kSamplesPerPpq;
        }
    }
    long long firstWith(int pitch, long long fromSample = 0) const {
        for (const auto& note : notes_) {
            if (note.pitch == pitch && note.sample >= fromSample) {
                return note.sample;
            }
        }
        return -1;
    }
    bool played(int pitch) const { return firstWith(pitch) >= 0; }

    mm::plugin::InstrumentProcessor processor;

private:
    FakePlayHead head_;
    juce::AudioBuffer<float> audio_;
    juce::MidiBuffer midi_;
    double ppq_ = 0.0;
    bool playing_ = true;
    std::vector<NoteOn> notes_;
};

/// A one-bar pattern whose bass plays `pitch` on every bar line.
mm::core::Pattern marked(uint8_t pitch) {
    mm::core::Pattern pattern = mm::core::makeEmptyPattern(1, "peak_time");
    mm::core::Note note;
    note.id = mm::core::allocateNoteId(pattern);
    note.pitch = pitch;
    note.startTick = 0;
    note.lengthTicks = 240;
    pattern.voices[0].notes.push_back(note);
    return pattern;
}

/// Two voices: the bass plays `bass`, the second voice plays `second`, both on every bar line.
mm::core::Pattern markedTwoVoices(uint8_t bass, uint8_t second) {
    mm::core::Pattern pattern = marked(bass);
    REQUIRE(pattern.voices.size() >= 2);
    mm::core::Note note = pattern.voices[0].notes[0];
    note.id = mm::core::allocateNoteId(pattern);
    note.pitch = second;
    pattern.voices[1].notes.push_back(note);
    return pattern;
}

/// Fills slots 1 to 3 of the hub with patterns that play pitch 41, 42 and 43.
void fillSlots(Instance& hub) {
    hub.processor.editSlots([](mm::core::SlotBank& bank) {
        for (size_t slot = 0; slot < 3; ++slot) {
            REQUIRE(bank.setResult(slot, marked(static_cast<uint8_t>(41 + slot))));
        }
    });
}

void runTo(std::vector<Instance*> instances, double ppq, bool hubFirst = true) {
    if (!hubFirst) {
        std::reverse(instances.begin(), instances.end());
    }
    while (instances.front()->ppq() < ppq - 1e-9) {
        for (auto* instance : instances) {
            instance->step();
        }
    }
}

long long at(double ppq) {
    return std::llround(ppq * kSamplesPerPpq);
}

bool near(long long a, long long b) {
    return std::llabs(a - b) <= 1;
}

struct Quiet {
    Quiet() { REQUIRE(mm::engine::processGroupChannel().memberCount() == 0); }
    juce::ScopedJuceInitialiser_GUI gui;
};

} // namespace

TEST_CASE("Z16 in the plugin: the hub's slot change near the bar line reaches hub and voices at the bar after",
          "[group-wiring]") {
    for (const bool hubFirst : {true, false}) {
        Quiet quiet;
        Instance hub("Hub");
        Instance voiceA("A");
        Instance voiceB("B");
        hub.setRole(InstanceRole::Hub);
        voiceA.setRole(InstanceRole::Voice);
        voiceB.setRole(InstanceRole::Voice);
        fillSlots(hub);
        runTo({&hub, &voiceA, &voiceB}, 7.8, hubFirst);
        hub.setSlotParameter(2);
        runTo({&hub, &voiceA, &voiceB}, 13.0, hubFirst);
        for (const Instance* instance : {&hub, &voiceA, &voiceB}) {
            INFO("hubFirst " << hubFirst);
            CHECK(near(instance->firstWith(42), at(12.0)));
            CHECK(instance->firstWith(41, at(8.0)) > 0);        // slot 1 still sounded at 8.0
            CHECK(instance->firstWith(41, at(12.0) - 1) == -1); // and never again from 12.0
        }
    }
}

TEST_CASE("a change with room switches at the next bar line for hub and voice alike", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    runTo({&hub, &voice}, 6.0);
    hub.setSlotParameter(3);
    runTo({&hub, &voice}, 13.0);
    CHECK(near(hub.firstWith(43), at(8.0)));
    CHECK(near(voice.firstWith(43), at(8.0)));
    CHECK(voice.processor.lateSwitches() == 0);
}

TEST_CASE("Z18 in the plugin: a voice set to own follows its own slot parameter, not the hub", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice, SlotFollow::Own);
    fillSlots(hub);
    runTo({&hub, &voice}, 7.0);
    voice.setSlotParameter(2);
    runTo({&hub, &voice}, 9.0);
    CHECK(near(voice.firstWith(42), at(8.0)));
    hub.setSlotParameter(3);
    runTo({&hub, &voice}, 17.0);
    CHECK(hub.played(43));
    CHECK_FALSE(voice.played(43));
}

TEST_CASE("a voice that follows the hub ignores its own slot parameter", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    runTo({&hub, &voice}, 3.0);
    voice.setSlotParameter(3);
    runTo({&hub, &voice}, 13.0);
    CHECK_FALSE(voice.played(43));
    CHECK(voice.played(41));
}

TEST_CASE("Z19 in the plugin: a start mid-arrangement takes the hub's slot at once or at the next bar",
          "[group-wiring]") {
    {
        Quiet quiet;
        Instance hub("Hub");
        Instance voice("Voice");
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice);
        fillSlots(hub);
        hub.setSlotParameter(3);
        hub.setPpq(148.0);
        voice.setPpq(148.0);
        hub.step(); // the hub is processed first
        voice.step();
        CHECK(near(voice.firstWith(43), at(148.0)));
        CHECK_FALSE(voice.played(41));
    }
    {
        Quiet quiet;
        Instance hub("Hub");
        Instance voice("Voice");
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice);
        fillSlots(hub);
        hub.setSlotParameter(3);
        hub.setPpq(148.0);
        voice.setPpq(148.0);
        runTo({&hub, &voice}, 153.0, false); // the voice always first
        CHECK(near(voice.firstWith(41), at(148.0)));
        CHECK(near(voice.firstWith(43), at(152.0)));
    }
}

TEST_CASE("when the hub is deleted the voice keeps the slot it has", "[group-wiring]") {
    Quiet quiet;
    Instance voice("Voice");
    {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice);
        fillSlots(hub);
        runTo({&hub, &voice}, 6.0);
        hub.setSlotParameter(2);
        runTo({&hub, &voice}, 9.0);
        REQUIRE(voice.played(42));
    }
    voice.setSlotParameter(3); // nobody listens to it in "follows hub"
    runTo({&voice}, 21.0);
    CHECK_FALSE(voice.played(43));
    CHECK(voice.firstWith(42, at(16.0)) == at(16.0));
}

TEST_CASE("a new hub is followed after the takeover", "[group-wiring]") {
    Quiet quiet;
    Instance voiceA("A");
    Instance voiceB("B");
    {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        voiceA.setRole(InstanceRole::Voice);
        voiceB.setRole(InstanceRole::Voice);
        fillSlots(hub);
    }
    voiceA.processor.acceptHubOffer(); // the oldest voice is offered the role
    REQUIRE(voiceA.processor.groupStatus() == GroupStatus::Hub);
    runTo({&voiceA, &voiceB}, 5.0);
    voiceA.setSlotParameter(2);
    runTo({&voiceA, &voiceB}, 13.0);
    CHECK(near(voiceA.firstWith(42), at(8.0)));
    CHECK(near(voiceB.firstWith(42), at(8.0)));
}

TEST_CASE("a voice that takes over the hub is a hub in its settings and state, and hands on its changes",
          "[group-wiring]") {
    Quiet quiet;
    Instance voiceA("A");
    Instance voiceB("B");
    {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        voiceA.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
        voiceB.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
        fillSlots(hub);
    }
    voiceA.processor.acceptHubOffer();
    CHECK(voiceA.processor.instanceSettings().role == InstanceRole::Hub);
    CHECK(voiceB.processor.instanceSettings().role == InstanceRole::Voice);

    // The new hub's later changes reach the other voice: they are distributed because it is a hub now.
    voiceA.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(5, marked(60)); });
    CHECK_FALSE(voiceB.processor.slotsSnapshot().isEmpty(5));

    // And its state says hub.
    juce::MemoryBlock saved;
    voiceA.processor.getStateInformation(saved);
    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    CHECK(root->getStringAttribute("role") == "hub");
}

TEST_CASE("the follow switch is saved, and a state without it means follows hub", "[group-wiring]") {
    Quiet quiet;
    Instance source("Source");
    source.setRole(InstanceRole::Voice, SlotFollow::Own, 2);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);

    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.instanceSettings().slotFollow == SlotFollow::Own);
    CHECK(target.processor.instanceSettings().role == InstanceRole::Voice);

    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->removeAttribute("slotFollow");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*root, old);
    Instance other("Other");
    other.setRole(InstanceRole::Voice, SlotFollow::Own);
    other.processor.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(other.processor.instanceSettings().slotFollow == SlotFollow::Hub);

    root->setAttribute("slotFollow", "sideways");
    juce::MemoryBlock odd;
    juce::AudioProcessor::copyXmlToBinary(*root, odd);
    other.processor.setStateInformation(odd.getData(), static_cast<int>(odd.getSize()));
    CHECK(other.processor.instanceSettings().slotFollow == SlotFollow::Hub);

    juce::MemoryBlock again;
    target.processor.getStateInformation(again);
    CHECK(again == saved);
}

TEST_CASE("only hub and voices take a slot in the group channel", "[group-wiring]") {
    Quiet quiet;
    auto& channel = mm::engine::processGroupChannel();
    {
        Instance solo("Solo");
        CHECK(channel.memberCount() == 0);
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        CHECK(channel.memberCount() == 1);
        CHECK(channel.hub() != mm::engine::GroupChannel::kNone);
        Instance voice("Voice");
        voice.setRole(InstanceRole::Voice);
        CHECK(channel.memberCount() == 2);
        Instance second("Second hub");
        second.setRole(InstanceRole::Hub); // refused: stays solo
        CHECK(second.processor.groupStatus() == GroupStatus::HubRefused);
        CHECK(channel.memberCount() == 2);
        voice.setRole(InstanceRole::Solo);
        CHECK(channel.memberCount() == 1);
        hub.setRole(InstanceRole::Solo);
        CHECK(channel.memberCount() == 0);
        CHECK(channel.hub() == mm::engine::GroupChannel::kNone);
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice);
        CHECK(channel.memberCount() == 2);
    }
    CHECK(channel.memberCount() == 0); // the destructors gave the slots back
    CHECK(channel.hub() == mm::engine::GroupChannel::kNone);
}

TEST_CASE("an instance that is not in a group plays exactly as before", "[group-wiring]") {
    Quiet quiet;
    Instance solo("Solo");
    fillSlots(solo);
    runTo({&solo}, 7.8);
    solo.setSlotParameter(2); // no lead rule without a group: the plain next bar line
    runTo({&solo}, 13.0);
    CHECK(near(solo.firstWith(42), at(8.0)));
}

TEST_CASE("the same inputs give the same notes in the group", "[group-wiring]") {
    const auto scenario = [] {
        Quiet quiet;
        Instance hub("Hub");
        Instance voice("Voice");
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice);
        fillSlots(hub);
        runTo({&hub, &voice}, 6.3);
        hub.setSlotParameter(2);
        runTo({&hub, &voice}, 14.0);
        std::vector<NoteOn> all = hub.notes();
        all.insert(all.end(), voice.notes().begin(), voice.notes().end());
        return all;
    };
    const auto first = scenario();
    const auto second = scenario();
    REQUIRE(first.size() == second.size());
    for (size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].sample == second[i].sample);
        CHECK(first[i].pitch == second[i].pitch);
    }
}

TEST_CASE("audio threads run while roles, switches and slots change on the message thread", "[group-wiring][stress]") {
    Quiet quiet;
    auto hub = std::make_unique<Instance>("Hub");
    auto voiceA = std::make_unique<Instance>("A");
    auto voiceB = std::make_unique<Instance>("B");
    hub->setRole(InstanceRole::Hub);
    voiceA->setRole(InstanceRole::Voice);
    voiceB->setRole(InstanceRole::Voice);
    fillSlots(*hub);

    std::atomic<bool> stop{false};
    const auto audio = [&](Instance* instance) {
        for (int block = 0; block < 4000 && !stop.load(); ++block) {
            instance->step();
        }
    };
    std::thread t1(audio, hub.get());
    std::thread t2(audio, voiceA.get());
    std::thread t3(audio, voiceB.get());
    for (int round = 0; round < 300; ++round) {
        hub->setSlotParameter(1 + round % 3);
        voiceA->setRole(round % 5 == 0 ? InstanceRole::Solo : InstanceRole::Voice,
                        round % 2 == 0 ? SlotFollow::Hub : SlotFollow::Own);
        voiceB->setRole(InstanceRole::Voice, round % 3 == 0 ? SlotFollow::Own : SlotFollow::Hub);
        if (round % 50 == 49) {
            hub->setRole(round % 100 == 49 ? InstanceRole::Solo : InstanceRole::Hub);
        }
        hub->setMute(1 + round % 2, round % 4 < 2); // mute works twice: the mask is read by the voices meanwhile
        voiceA->setMute(1, round % 3 == 0);
        if (round % 25 == 0) {
            auto settings = hub->processor.instanceSettings();
            settings.outputMode = round % 50 == 0 ? mm::core::OutputMode::None : mm::core::OutputMode::OneVoice;
            hub->processor.setInstanceSettings(settings);
            voiceB->processor.generate(); // forwarded to the hub while the audio threads run
        }
        Instance extra("Extra"); // instances that come and go meanwhile
        extra.setRole(InstanceRole::Voice);
    }
    stop.store(true);
    t1.join();
    t2.join();
    t3.join();
    hub.reset();
    voiceA.reset();
    voiceB.reset();
    CHECK(mm::engine::processGroupChannel().memberCount() == 0);
}

TEST_CASE("a pattern change by the hub shortly before the bar line reaches hub and voices at the same bar",
          "[group-wiring]") {
    // The voice runs half a quarter ahead of the hub (it has passed bar 8 when the hub still has 0.2 to go) or behind
    // it (it would switch at bar 8 on its own while the hub waits for bar 12).
    for (const bool voiceAhead : {true, false}) {
        INFO("voiceAhead " << voiceAhead);
        Quiet quiet;
        Instance hub("Hub");
        Instance voice("Voice");
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice);
        fillSlots(hub);
        (voiceAhead ? voice : hub).setPpq(0.5);
        runTo({&hub, &voice}, 7.8);
        hub.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, marked(50)); });
        runTo({&hub, &voice}, 13.0);
        for (const Instance* instance : {&hub, &voice}) {
            CHECK(near(instance->firstWith(50), at(12.0)));
            CHECK(instance->firstWith(41, at(8.0)) > 0);
            CHECK(instance->firstWith(41, at(12.0) - 1) == -1);
        }
    }
}

TEST_CASE("a pattern change by a hub without company takes effect at the next bar line", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    fillSlots(hub);
    runTo({&hub}, 7.8);
    hub.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, marked(50)); });
    runTo({&hub}, 9.0);
    CHECK(near(hub.firstWith(50), at(8.0)));
}

TEST_CASE("a pattern change while nobody plays has no stamp and plays from the start", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    hub.setPlaying(false);
    voice.setPlaying(false);
    hub.step();
    voice.step();
    hub.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, marked(50)); });
    hub.setPlaying(true);
    voice.setPlaying(true);
    runTo({&hub, &voice}, 1.0);
    CHECK(near(hub.firstWith(50), at(0.0)));
    CHECK(near(voice.firstWith(50), at(0.0)));
}

TEST_CASE("mute works twice: the hub's switch silences the voice, its own switch too", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice1("Voice 1");
    Instance voice2("Voice 2");
    hub.setRole(InstanceRole::Hub);
    voice1.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    voice2.setRole(InstanceRole::Voice, SlotFollow::Hub, 2);
    hub.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(41, 71)); });
    runTo({&hub, &voice1, &voice2}, 5.0);
    REQUIRE(voice1.played(41));
    REQUIRE(voice2.played(71));

    hub.setMute(2, true); // the hub mutes voice 2 only
    runTo({&hub, &voice1, &voice2}, 9.0);
    CHECK(voice1.firstWith(41, at(8.0)) >= 0);
    CHECK(voice2.firstWith(71, at(8.0)) == -1);
    CHECK(hub.firstWith(41, at(8.0)) >= 0);

    hub.setMute(2, false);
    runTo({&hub, &voice1, &voice2}, 13.0);
    CHECK(voice2.firstWith(71, at(12.0)) >= 0);

    voice1.setMute(1, true); // its own switch
    runTo({&hub, &voice1, &voice2}, 17.0);
    CHECK(voice1.firstWith(41, at(16.0)) == -1);
    CHECK(voice2.firstWith(71, at(16.0)) >= 0);
}

TEST_CASE("a voice is audible again when the hub that muted it is gone", "[group-wiring]") {
    Quiet quiet;
    Instance voice("Voice");
    {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
        hub.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, marked(41)); });
        hub.setMute(1, true);
        runTo({&hub, &voice}, 5.0);
        CHECK_FALSE(voice.played(41));
    }
    runTo({&voice}, 9.0);
    CHECK(voice.firstWith(41, at(8.0)) >= 0);
}

TEST_CASE("a hub with output none plays nothing and the voices go on", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    runTo({&hub, &voice}, 5.0);
    REQUIRE(hub.played(41));
    auto settings = hub.processor.instanceSettings();
    settings.outputMode = mm::core::OutputMode::None;
    hub.processor.setInstanceSettings(settings);
    hub.setSlotParameter(2);
    runTo({&hub, &voice}, 13.0);
    CHECK(hub.firstWith(41, at(8.0)) == -1);
    CHECK(hub.firstWith(42, 0) == -1);
    CHECK(near(voice.firstWith(42), at(8.0))); // the voice still follows the hub's slot
    settings.outputMode = mm::core::OutputMode::OneVoice;
    hub.processor.setInstanceSettings(settings);
    runTo({&hub, &voice}, 17.0);
    CHECK(hub.firstWith(42, at(16.0)) >= 0);
}

TEST_CASE("output none only silences a hub", "[group-wiring]") {
    Quiet quiet;
    Instance solo("Solo");
    fillSlots(solo);
    auto settings = solo.processor.instanceSettings();
    settings.outputMode = mm::core::OutputMode::None;
    solo.processor.setInstanceSettings(settings);
    runTo({&solo}, 5.0);
    CHECK(solo.played(41));
}

TEST_CASE("a voice's generate request is carried out by the hub with the hub's settings", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    auto settings = hub.processor.instanceSettings();
    settings.generation.lengthBars = 2;
    hub.processor.setInstanceSettings(settings);
    REQUIRE(voice.processor.groupStatus() == GroupStatus::VoiceConnected);
    voice.processor.generate();
    CHECK(voice.processor.generationStatus() == mm::plugin::GenerationStatus::Forwarded);
    CHECK(hub.processor.generationStatus() == mm::plugin::GenerationStatus::Generating);
    for (int i = 0; i < 300 && voice.processor.slotsSnapshot().isEmpty(0); ++i) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    REQUIRE_FALSE(hub.processor.slotsSnapshot().isEmpty(0));
    REQUIRE_FALSE(voice.processor.slotsSnapshot().isEmpty(0));
    CHECK(voice.processor.slotsSnapshot().slot(0)->pattern->lengthBars == 2);
}

TEST_CASE("only a voice that finds no hub gets the hint about separate plug-in processes", "[group-wiring]") {
    using mm::plugin::groupStatusText;
    const auto hint = [](GroupStatus status) {
        return groupStatusText(status, 0).find("separate processes") != std::string::npos;
    };
    CHECK(hint(GroupStatus::HubMissing));
    CHECK(hint(GroupStatus::HubOffered));
    CHECK_FALSE(hint(GroupStatus::VoiceConnected));
    CHECK_FALSE(hint(GroupStatus::Hub));
    CHECK_FALSE(hint(GroupStatus::HubRefused));
    CHECK_FALSE(hint(GroupStatus::Solo));
    CHECK(groupStatusText(GroupStatus::Hub, 1) == "Hub: 1 voice");
    CHECK(groupStatusText(GroupStatus::Hub, 3) == "Hub: 3 voices");
    CHECK(groupStatusText(GroupStatus::Solo, 0).empty());
    CHECK(groupStatusText(GroupStatus::HubOffered, 0).find("you can take over") != std::string::npos);
}

TEST_CASE("a voice's octave is its own: it shifts that voice and nothing else", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voiceUp("Up");
    Instance voicePlain("Plain");
    hub.setRole(InstanceRole::Hub);
    voiceUp.setRole(InstanceRole::Voice);
    voicePlain.setRole(InstanceRole::Voice);
    auto settings = voiceUp.processor.instanceSettings();
    settings.octave = 1;
    voiceUp.processor.setInstanceSettings(settings);
    fillSlots(hub);
    runTo({&hub, &voiceUp, &voicePlain}, 5.0);
    CHECK(voiceUp.played(41 + 12));
    CHECK_FALSE(voiceUp.played(41));
    CHECK(voicePlain.played(41));
    CHECK(hub.played(41));
    CHECK(hub.processor.instanceSettings().octave == 0); // the hub is not told

    settings.octave = -1; // a change of the octave republishes the slots of that instance
    voiceUp.processor.setInstanceSettings(settings);
    runTo({&hub, &voiceUp, &voicePlain}, 13.0);
    CHECK(voiceUp.firstWith(41 - 12, at(8.0)) >= 0);
    CHECK(voiceUp.firstWith(41 + 12, at(8.0)) == -1);
}

TEST_CASE("the octave is saved, limited, and missing in older states", "[group-wiring]") {
    Quiet quiet;
    Instance source("Source");
    auto settings = source.processor.instanceSettings();
    settings.octave = -2;
    source.processor.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.instanceSettings().octave == -2);

    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("octave", 7);
    juce::MemoryBlock far;
    juce::AudioProcessor::copyXmlToBinary(*root, far);
    target.processor.setStateInformation(far.getData(), static_cast<int>(far.getSize()));
    CHECK(target.processor.instanceSettings().octave == 2);

    root->removeAttribute("octave");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*root, old);
    target.processor.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(target.processor.instanceSettings().octave == 0);

    settings.octave = 5; // out of range through the setter
    target.processor.setInstanceSettings(settings);
    CHECK(target.processor.instanceSettings().octave == 2);
}

TEST_CASE("a voice's Open hub request reaches the hub's window, nobody else's", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    Instance solo("Solo");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    voice.processor.showHub();
    CHECK(hub.processor.frontRequests() == 1);
    CHECK(voice.processor.frontRequests() == 0);
    CHECK(solo.processor.frontRequests() == 0);
    hub.processor.showHub(); // a hub asks nobody
    CHECK(hub.processor.frontRequests() == 1);
}

TEST_CASE("an Open hub request without a hub does nothing", "[group-wiring]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    voice.processor.showHub();
    CHECK(voice.processor.frontRequests() == 0);
}

TEST_CASE("the playing voice is what the roll shows and the export writes", "[group-wiring]") {
    Quiet quiet;
    Instance instance("Instance");
    CHECK_FALSE(instance.processor.playingVoice().hasPattern); // an empty slot shows nothing

    instance.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(41, 71)); });
    const auto bass = instance.processor.playingVoice();
    REQUIRE(bass.hasPattern);
    CHECK(bass.slot == 1);
    CHECK(bass.voiceName == "Bass");
    REQUIRE_FALSE(bass.notes.empty());
    CHECK(bass.notes.front().pitch == 41);
    CHECK(bass.lengthTicks == 3840);

    instance.setRole(InstanceRole::Solo, SlotFollow::Hub, 2);
    const auto melody = instance.processor.playingVoice();
    CHECK(melody.voiceName == "Melody");
    REQUIRE_FALSE(melody.notes.empty());
    CHECK(melody.notes.front().pitch == 71);
    CHECK_FALSE(melody.sameSource(bass));

    auto settings = instance.processor.instanceSettings();
    settings.octave = 1;
    instance.processor.setInstanceSettings(settings);
    const auto up = instance.processor.playingVoice();
    CHECK(up.notes.front().pitch == 71 + 12);
    CHECK_FALSE(up.sameSource(melody));
    CHECK(instance.processor.playingVoice().sameSource(up)); // nothing changed since

    instance.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(42, 72)); });
    CHECK_FALSE(instance.processor.playingVoice().sameSource(up)); // a new revision
}

TEST_CASE("the mute state names its source", "[group-wiring]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 2);
    fillSlots(hub);
    runTo({&hub, &voice}, 1.0);
    CHECK_FALSE(voice.processor.mutedByOwnSwitch());
    CHECK_FALSE(voice.processor.mutedByHub());
    hub.setMute(2, true);
    runTo({&hub, &voice}, 2.0);
    CHECK(voice.processor.mutedByHub());
    CHECK_FALSE(voice.processor.mutedByOwnSwitch());
    CHECK_FALSE(hub.processor.mutedByHub()); // a hub only has its own switch
    voice.setMute(2, true);
    CHECK(voice.processor.mutedByOwnSwitch());
}

namespace {

void pumpMessages(int milliseconds) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

/// A component below `parent` (at any depth) by component id.
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

template <typename T> T* child(juce::Component& parent, const juce::String& id) {
    return dynamic_cast<T*>(findById(parent, id));
}

} // namespace

TEST_CASE("the editor of a voice is the compact voice UI, other roles get the hub UI", "[group-wiring][editor]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() == 600);
    CHECK(editor->getHeight() == 320);
    CHECK(child<juce::ToggleButton>(*editor, "mute")->isVisible());
    CHECK(child<juce::Component>(*editor, "roll")->isVisible());
    CHECK(child<juce::ComboBox>(*editor, "octave")->isVisible());
    CHECK(child<juce::TextButton>(*editor, "openHub")->isVisible());

    voice.setRole(InstanceRole::Solo);
    pumpMessages(500); // the editor's timer notices the new role
    CHECK(editor->getWidth() == 1000);
    CHECK(editor->getHeight() == 640);
    CHECK_FALSE(child<juce::ToggleButton>(*editor, "mute")->isVisible());
    CHECK_FALSE(child<juce::Component>(*editor, "roll")->isVisible());

    voice.setRole(InstanceRole::Voice);
    pumpMessages(500);
    CHECK(editor->getHeight() == 320);
    CHECK(child<juce::ToggleButton>(*editor, "mute")->isVisible());

    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> soloEditor(solo.processor.createEditor());
    CHECK(soloEditor->getWidth() == 1000);
    CHECK(soloEditor->getHeight() == 640);
    CHECK_FALSE(child<juce::Component>(*soloEditor, "roll")->isVisible());
}

TEST_CASE("the mute button is the host parameter of the voice, both ways", "[group-wiring][editor]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* mute = child<juce::ToggleButton>(*editor, "mute");
    REQUIRE(mute != nullptr);
    auto* parameter = voice.processor.parameters().getParameter("mute_2");
    REQUIRE(parameter != nullptr);
    CHECK_FALSE(mute->getToggleState());

    mute->setToggleState(true, juce::sendNotificationSync);
    CHECK(parameter->getValue() == 1.0f);
    CHECK(voice.processor.parameters().getRawParameterValue("mute_1")->load() == 0.0f); // only its own voice
    CHECK(voice.processor.mutedByOwnSwitch());

    parameter->setValueNotifyingHost(0.0f); // automation moves the switch
    pumpMessages(100);
    CHECK_FALSE(mute->getToggleState());

    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 1); // another voice: the button follows that parameter
    pumpMessages(500);
    mute->setToggleState(true, juce::sendNotificationSync);
    CHECK(voice.processor.parameters().getRawParameterValue("mute_1")->load() == 1.0f);
    CHECK(parameter->getValue() == 0.0f);
}

TEST_CASE("the octave box sets the octave of the instance and shows it", "[group-wiring][editor]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* box = child<juce::ComboBox>(*editor, "octave");
    REQUIRE(box != nullptr);
    CHECK(box->getSelectedId() == 3); // octave 0
    box->setSelectedId(5, juce::sendNotificationSync);
    CHECK(voice.processor.instanceSettings().octave == 2);
    box->setSelectedId(1, juce::sendNotificationSync);
    CHECK(voice.processor.instanceSettings().octave == -2);

    auto settings = voice.processor.instanceSettings();
    settings.octave = 1; // set from outside: the box follows
    voice.processor.setInstanceSettings(settings);
    pumpMessages(500);
    CHECK(box->getSelectedId() == 4);
}

TEST_CASE("the Open hub button works only with a hub", "[group-wiring][editor]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* button = child<juce::TextButton>(*editor, "openHub");
    REQUIRE(button != nullptr);
    CHECK_FALSE(button->isEnabled());
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    pumpMessages(500);
    CHECK(button->isEnabled());
    button->triggerClick(); // asynchronous
    pumpMessages(200);
    CHECK(hub.processor.frontRequests() == 1);
}

namespace {

/// A child of the editor or of one of its rows, by component id.
juce::Component* find(juce::Component& editor, const juce::String& id) {
    return findById(editor, id);
}

} // namespace

TEST_CASE("the hub UI fields show the settings and change them", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    auto* key = child<juce::ComboBox>(*editor, "key");
    auto* scale = child<juce::ComboBox>(*editor, "scale");
    auto* bars = child<juce::ComboBox>(*editor, "bars");
    auto* seed = child<juce::TextEditor>(*editor, "seed");
    auto* energy = child<juce::Slider>(*editor, "energy");
    auto* creativity = child<juce::Slider>(*editor, "creativity");
    auto* style = child<juce::ComboBox>(*editor, "style");
    REQUIRE(key != nullptr);
    REQUIRE(scale != nullptr);
    REQUIRE(bars != nullptr);
    REQUIRE(seed != nullptr);
    REQUIRE(energy != nullptr);
    REQUIRE(creativity != nullptr);
    REQUIRE(style != nullptr);
    CHECK(key->isVisible());

    // A new instance: A minor, 4 bars, a random seed.
    CHECK(key->getText() == "Key: A");
    CHECK(scale->getText() == "Scale: Natural minor");
    CHECK(bars->getSelectedId() == 4);
    CHECK(seed->getText().isEmpty());
    CHECK(energy->getValue() == 50.0);
    CHECK(creativity->getValue() == 40.0);

    key->setSelectedId(4, juce::sendNotificationSync); // D
    CHECK(solo.processor.instanceSettings().generation.root == 2);
    key->setSelectedId(1, juce::sendNotificationSync); // auto
    CHECK_FALSE(solo.processor.instanceSettings().generation.root.has_value());

    scale->setSelectedId(3, juce::sendNotificationSync);
    const auto scales = mm::core::allScales();
    CHECK(solo.processor.instanceSettings().generation.scaleId == std::string(scales[1].id));
    scale->setSelectedId(1, juce::sendNotificationSync);
    CHECK_FALSE(solo.processor.instanceSettings().generation.scaleId.has_value());

    bars->setSelectedId(16, juce::sendNotificationSync);
    CHECK(solo.processor.instanceSettings().generation.lengthBars == 16);

    energy->setValue(80.0, juce::sendNotificationSync);
    creativity->setValue(10.0, juce::sendNotificationSync);
    CHECK(solo.processor.instanceSettings().generation.energyPct == 80);
    CHECK(solo.processor.instanceSettings().generation.creativityPct == 10);

    style->setSelectedId(2, juce::sendNotificationSync);
    CHECK(solo.processor.instanceSettings().generation.styleId == solo.processor.styles().profiles()[1].id);

    seed->setText("4711", juce::dontSendNotification);
    seed->onReturnKey();
    CHECK(solo.processor.instanceSettings().generation.seed == 4711);
    seed->setText("", juce::dontSendNotification);
    seed->onReturnKey();
    CHECK_FALSE(solo.processor.instanceSettings().generation.seed.has_value());
    seed->setText("99999999999999999999999", juce::dontSendNotification); // the box limits the length, not the value
    seed->onReturnKey();
    CHECK_FALSE(solo.processor.instanceSettings().generation.seed.has_value());
    CHECK(seed->getText().isEmpty()); // back to "random"

    seed->setText("5", juce::dontSendNotification);
    seed->onReturnKey();
    auto* random = child<juce::TextButton>(*editor, "random");
    REQUIRE(random != nullptr);
    random->triggerClick();
    pumpMessages(100);
    CHECK_FALSE(solo.processor.instanceSettings().generation.seed.has_value());
    CHECK(seed->getText().isEmpty());
}

TEST_CASE("settings changed from outside show up in the hub UI", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    auto settings = solo.processor.instanceSettings();
    settings.generation.root = 7;
    settings.generation.scaleId = "dorian";
    settings.generation.lengthBars = 8;
    settings.generation.seed = 123;
    settings.generation.energyPct = 90;
    solo.processor.setInstanceSettings(settings);
    pumpMessages(500);
    CHECK(child<juce::ComboBox>(*editor, "key")->getText() == "Key: G");
    CHECK(child<juce::ComboBox>(*editor, "scale")->getText() == "Scale: Dorian");
    CHECK(child<juce::ComboBox>(*editor, "bars")->getSelectedId() == 8);
    CHECK(child<juce::TextEditor>(*editor, "seed")->getText() == "123");
    CHECK(child<juce::Slider>(*editor, "energy")->getValue() == 90.0);
}

TEST_CASE("the slot strip selects the slot and shows which slots are filled", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(2, marked(41)); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    pumpMessages(300);
    auto* first = child<juce::TextButton>(*editor, "slot1");
    auto* third = child<juce::TextButton>(*editor, "slot3");
    auto* last = child<juce::TextButton>(*editor, "slot16");
    REQUIRE(first != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(last != nullptr);
    CHECK(first->getToggleState()); // slot 1 is the selected one
    CHECK_FALSE(third->getToggleState());
    const auto colour = [](juce::TextButton* button) { return button->findColour(juce::TextButton::buttonColourId); };
    CHECK(colour(third) != colour(first)); // filled and empty look different
    CHECK(colour(first) == colour(last));

    third->triggerClick();
    pumpMessages(300);
    CHECK(solo.processor.activeSlot() == 3);
    CHECK(third->getToggleState());
    CHECK_FALSE(first->getToggleState());
    for (int slot = 1; slot <= 16; ++slot) {
        CHECK(child<juce::TextButton>(*editor, "slot" + juce::String(slot)) != nullptr);
    }
}

TEST_CASE("the hub UI has a row per voice of the playing slot", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    pumpMessages(300);
    // An empty slot: the two default voices, without notes.
    REQUIRE(find(*editor, "row_1") != nullptr);
    REQUIRE(find(*editor, "row_2") != nullptr);
    CHECK(find(*editor, "row_3") == nullptr);
    CHECK(dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_1"))->layout().notes.empty());

    solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(41, 71)); });
    pumpMessages(500);
    auto* bass = dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_1"));
    auto* melody = dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_2"));
    REQUIRE(bass != nullptr);
    REQUIRE(melody != nullptr);
    CHECK_FALSE(bass->layout().notes.empty());
    CHECK_FALSE(melody->layout().notes.empty());
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "name_1"))->getText() == "Bass");
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "name_2"))->getText() == "Melody");
    CHECK(find(*editor, "row_1")->getBounds().getBottom() <= editor->getHeight());
    CHECK(find(*editor, "row_2")->getBounds().getBottom() <= editor->getHeight());

    // A pattern with three voices gets three rows.
    mm::core::Pattern three = markedTwoVoices(41, 71);
    three.voices.push_back(three.voices[1]);
    three.voices[2].midiChannel = 3; // channels are unique
    for (auto& note : three.voices[2].notes) {
        note.id = mm::core::allocateNoteId(three);
    }
    solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(1, three)); });
    solo.setSlotParameter(2);
    runTo({&solo}, 4.1); // a slot change takes effect at the next bar line
    pumpMessages(500);
    REQUIRE(find(*editor, "row_3") != nullptr);
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "name_3"))->getText() == "Voice 3");

    // Back to a slot with two voices: two rows again.
    solo.setSlotParameter(1);
    runTo({&solo}, 8.1);
    pumpMessages(500);
    CHECK(find(*editor, "row_3") == nullptr);
}

TEST_CASE("each voice row has its own Mute and its own grip", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(41, 71)); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    pumpMessages(500);
    auto* mute2 = child<juce::ToggleButton>(*editor, "mute_2");
    REQUIRE(mute2 != nullptr);
    mute2->setToggleState(true, juce::sendNotificationSync);
    CHECK(solo.processor.parameters().getRawParameterValue("mute_2")->load() == 1.0f);
    CHECK(solo.processor.parameters().getRawParameterValue("mute_1")->load() == 0.0f);
    solo.setMute(1, true); // automation of the other voice moves its button only
    pumpMessages(100);
    CHECK(child<juce::ToggleButton>(*editor, "mute_1")->getToggleState());
    solo.setMute(2, false);
    pumpMessages(100);
    CHECK_FALSE(mute2->getToggleState());

    auto* grip1 = dynamic_cast<mm::plugin::DragHandle*>(find(*editor, "drag_1"));
    auto* grip2 = dynamic_cast<mm::plugin::DragHandle*>(find(*editor, "drag_2"));
    REQUIRE(grip1 != nullptr);
    REQUIRE(grip2 != nullptr);
    for (int i = 0; i < 400 && (grip1->file() == juce::File() || grip2->file() == juce::File()); ++i) {
        pumpMessages(10);
    }
    REQUIRE(grip1->file() != juce::File());
    REQUIRE(grip2->file() != juce::File());
    CHECK(grip1->file() == solo.processor.midiExporter().readyFile(1));
    CHECK(grip2->file() == solo.processor.midiExporter().readyFile(2));
    CHECK(grip1->file() != grip2->file());
}

TEST_CASE("the info line names what the playing slot holds", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    pumpMessages(300);
    auto* info = dynamic_cast<juce::Label*>(find(*editor, "info"));
    REQUIRE(info != nullptr);
    CHECK(info->getText().contains("empty"));
    mm::core::Pattern pattern = marked(41);
    pattern.context.root = 2;
    pattern.context.scaleId = "dorian";
    pattern.info.seed = 777;
    pattern.info.winnerSeed = 888;
    solo.processor.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(0, pattern); });
    pumpMessages(500);
    CHECK(info->getText().contains("Slot 1"));
    CHECK(info->getText().contains("D dorian"));
    CHECK(info->getText().contains("seed 777"));
    CHECK(info->getText().contains("winner 888"));
}

TEST_CASE("Generate in the hub UI uses the key, scale, bars and seed of its fields", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    child<juce::ComboBox>(*editor, "key")->setSelectedId(4, juce::sendNotificationSync); // D
    child<juce::ComboBox>(*editor, "scale")->setSelectedId(4, juce::sendNotificationSync);
    child<juce::ComboBox>(*editor, "bars")->setSelectedId(2, juce::sendNotificationSync);
    auto* seed = child<juce::TextEditor>(*editor, "seed");
    seed->setText("77", juce::dontSendNotification);
    seed->onReturnKey();
    const auto scales = mm::core::allScales();
    solo.processor.generate();
    for (int i = 0; i < 400 && solo.processor.slotsSnapshot().isEmpty(0); ++i) {
        pumpMessages(10);
    }
    const auto bank = solo.processor.slotsSnapshot();
    REQUIRE_FALSE(bank.isEmpty(0));
    const auto& pattern = *bank.slot(0)->pattern;
    CHECK(pattern.context.root == 2);
    CHECK(pattern.context.scaleId == std::string(scales[2].id));
    CHECK(pattern.lengthBars == 2);
    CHECK(pattern.info.seed == 77);
    pumpMessages(500);
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "info"))->getText().contains("seed 77"));
}
