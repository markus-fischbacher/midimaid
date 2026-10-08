#include "core/PatternGenerator.h"
#include "engine/GroupChannel.h"
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
