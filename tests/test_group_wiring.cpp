#include "NoUserSettings.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "engine/GroupChannel.h"
#include "plugin/EditableRoll.h"
#include "plugin/EditorParts.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/GroupText.h"
#include "plugin/KeyChords.h"
#include "plugin/LibraryStore.h"
#include "plugin/PlaceholderEditor.h"
#include "plugin/ProcessorBase.h"
#include "plugin/ReferenceStore.h"
#include "plugin/UserGrooveStore.h"
#include "plugin/EmbeddedGrooves.h"

#include <algorithm>
#include <catch2/catch_approx.hpp>
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
    int velocity = 0;
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
    /// MIDI the host delivers to the plug-in in the next block (a keyboard): note on or off at a sample of the block.
    void keyDown(int note, int channel = 1, int samplePosition = 0, int velocity = 100) {
        input_.addEvent(juce::MidiMessage::noteOn(channel, note, static_cast<juce::uint8>(velocity)), samplePosition);
    }
    void keyUp(int note, int channel = 1, int samplePosition = 0) {
        input_.addEvent(juce::MidiMessage::noteOff(channel, note), samplePosition);
    }
    double ppq() const { return ppq_; }
    const std::vector<NoteOn>& notes() const { return notes_; }
    const std::vector<NoteOn>& noteOffs() const { return offs_; }

    void step() {
        head_.info.setIsPlaying(playing_);
        head_.info.setBpm(120.0);
        head_.info.setPpqPosition(ppq_);
        audio_.clear();
        midi_.clear();
        midi_.addEvents(input_, 0, kBlock, 0);
        input_.clear();
        processor.processBlock(audio_, midi_);
        for (const auto metadata : midi_) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                notes_.push_back({std::llround(ppq_ * kSamplesPerPpq) + metadata.samplePosition,
                                  message.getNoteNumber(), message.getVelocity()});
            } else if (message.isNoteOff()) {
                offs_.push_back(
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
    juce::MidiBuffer input_;
    double ppq_ = 0.0;
    bool playing_ = true;
    std::vector<NoteOn> notes_;
    std::vector<NoteOn> offs_;
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
        return groupStatusText(status, 0).find("getrennten Prozessen") != std::string::npos;
    };
    CHECK(hint(GroupStatus::HubMissing));
    CHECK(hint(GroupStatus::HubOffered));
    CHECK_FALSE(hint(GroupStatus::VoiceConnected));
    CHECK_FALSE(hint(GroupStatus::Hub));
    CHECK_FALSE(hint(GroupStatus::HubRefused));
    CHECK_FALSE(hint(GroupStatus::Solo));
    CHECK(groupStatusText(GroupStatus::Hub, 1) == "Hub: 1 Stimme");
    CHECK(groupStatusText(GroupStatus::Hub, 3) == "Hub: 3 Stimmen");
    CHECK(groupStatusText(GroupStatus::Solo, 0).empty());
    CHECK(groupStatusText(GroupStatus::HubOffered, 0).find("du kannst ihn übernehmen") != std::string::npos);
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

TEST_CASE("the size of the window is saved with the instance, limited to 75 to 200, and missing in older states",
          "[group-wiring][scale]") {
    Quiet quiet;
    Instance source("Source");
    CHECK(source.processor.uiScalePct() == 100);
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("uiScale")); // the standard size leaves the state as it was

    source.processor.setUiScalePct(150);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.uiScalePct() == 150);

    root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    root->setAttribute("uiScale", 900);
    juce::MemoryBlock far;
    juce::AudioProcessor::copyXmlToBinary(*root, far);
    target.processor.setStateInformation(far.getData(), static_cast<int>(far.getSize()));
    CHECK(target.processor.uiScalePct() == 200);
    root->setAttribute("uiScale", 130);
    juce::MemoryBlock between;
    juce::AudioProcessor::copyXmlToBinary(*root, between);
    target.processor.setStateInformation(between.getData(), static_cast<int>(between.getSize()));
    CHECK(target.processor.uiScalePct() == 130); // between two steps: kept
    root->setAttribute("uiScale", 10);
    juce::MemoryBlock tiny;
    juce::AudioProcessor::copyXmlToBinary(*root, tiny);
    target.processor.setStateInformation(tiny.getData(), static_cast<int>(tiny.getSize()));
    CHECK(target.processor.uiScalePct() == 75);
    root->removeAttribute("uiScale");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*root, old);
    target.processor.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
    CHECK(target.processor.uiScalePct() == 100);
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

/// Pumps the message loop until `condition` holds (the editor's timer and the parameter attachments are asynchronous,
/// and a sanitizer build is slow), at most `timeoutMs`.
bool waitFor(const std::function<bool()>& condition, int timeoutMs = 5000) {
    for (int waited = 0; waited < timeoutMs && !condition(); waited += 20) {
        pumpMessages(20);
    }
    return condition();
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

TEST_CASE("a group line warns when AU and VST3 instances share the process", "[group-wiring][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.processor.setFormatTag("au");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    REQUIRE(editor != nullptr);
    const auto statusText = [&] { return child<juce::Label>(*editor, "status")->getText(); };
    const auto warns = [&] { return statusText().contains("AU- und VST3"); };
    CHECK_FALSE(hub.processor.otherFormatInProcess());
    Instance other("Other");
    other.processor.setFormatTag("au"); // the same format: no warning
    CHECK_FALSE(hub.processor.otherFormatInProcess());
    CHECK_FALSE(warns());
    other.processor.setFormatTag("vst3");
    CHECK(hub.processor.otherFormatInProcess());
    CHECK(waitFor(warns));
    other.setRole(InstanceRole::Solo);
    hub.setRole(InstanceRole::Solo); // a solo instance is in no group: no warning
    CHECK(waitFor([&] { return !warns(); }));
    hub.setRole(InstanceRole::Hub);
    CHECK(waitFor(warns));
    other.processor.setFormatTag(""); // the instance leaves the count
    CHECK_FALSE(hub.processor.otherFormatInProcess());
    CHECK(waitFor([&] { return !warns(); }));
}

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
    REQUIRE(waitFor([&] { return editor->getHeight() == 720; })); // the editor's timer notices the new role
    CHECK(editor->getWidth() == 1000);
    CHECK(editor->getHeight() == 720);
    CHECK_FALSE(child<juce::ToggleButton>(*editor, "mute")->isVisible());
    CHECK_FALSE(child<juce::Component>(*editor, "roll")->isVisible());

    voice.setRole(InstanceRole::Voice);
    REQUIRE(waitFor([&] { return editor->getHeight() == 320; }));
    CHECK(child<juce::ToggleButton>(*editor, "mute")->isVisible());

    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> soloEditor(solo.processor.createEditor());
    CHECK(soloEditor->getWidth() == 1000);
    CHECK(soloEditor->getHeight() == 720);
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
    CHECK(waitFor([&] { return !mute->getToggleState(); }));

    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 1); // another voice: the button follows that parameter
    voice.setMute(1, true);
    REQUIRE(waitFor([&] { return mute->getToggleState(); })); // bound to mute_1 now
    voice.setMute(1, false);
    REQUIRE(waitFor([&] { return !mute->getToggleState(); }));
    mute->setToggleState(true, juce::sendNotificationSync);
    CHECK(voice.processor.parameters().getRawParameterValue("mute_1")->load() == 1.0f);
    CHECK(parameter->getValue() == 0.0f);
}

TEST_CASE("the size box scales the window and keeps its aspect ratio, in the hub and in a voice",
          "[group-wiring][editor][scale]") {
    Quiet quiet;
    for (const bool voice : {false, true}) {
        Instance instance("Window");
        if (voice) {
            instance.setRole(InstanceRole::Voice);
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        auto* box = child<juce::ComboBox>(*editor, "zoom");
        REQUIRE(box != nullptr);
        CHECK(box->isVisible());
        CHECK(box->getSelectedId() == 100);
        const int width = editor->getWidth();
        const int height = editor->getHeight();
        CHECK(editor->getTransform().isIdentity());

        box->setSelectedId(150, juce::sendNotificationSync);
        CHECK(instance.processor.uiScalePct() == 150);
        CHECK(editor->getTransform().mat00 == Catch::Approx(1.5f));
        CHECK(editor->getWidth() == width); // the layout keeps its size, the window grows with the transform
        CHECK(editor->getHeight() == height);
        box->setSelectedId(75, juce::sendNotificationSync);
        CHECK(editor->getTransform().mat00 == Catch::Approx(0.75f));

        // the box is as high as the other boxes (a thin box shrinks its text, seen in the voice window)
        auto* role = child<juce::ComboBox>(*editor, "role");
        REQUIRE(role != nullptr);
        CHECK(box->getHeight() == role->getHeight());
        CHECK(box->getWidth() >= 100);

        // the box lies inside the window and on no other control
        const auto zoom = box->getBounds();
        CHECK(editor->getLocalBounds().contains(zoom));
        for (auto* other : editor->getChildren()) {
            if (other != box && other->isVisible() && other->getWidth() > 0) {
                INFO((voice ? "voice: " : "hub: ") << other->getComponentID().toStdString());
                if (other->getY() < 60 || other->getBottom() > editor->getHeight() - 60) {
                    CHECK_FALSE(zoom.intersects(other->getBounds()));
                }
            }
        }
    }
}

TEST_CASE("a window opened after the state was loaded starts at the saved size", "[group-wiring][editor][scale]") {
    Quiet quiet;
    Instance source("Source");
    source.processor.setUiScalePct(200);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> editor(target.processor.createEditor());
    CHECK(child<juce::ComboBox>(*editor, "zoom")->getSelectedId() == 200);
    CHECK(editor->getTransform().mat00 == Catch::Approx(2.0f));
}

TEST_CASE("dragging the corner sets the size continuously, in the hub and in a voice",
          "[group-wiring][editor][scale]") {
    Quiet quiet;
    for (const bool voice : {false, true}) {
        INFO((voice ? "voice" : "hub"));
        Instance instance("Window");
        if (voice) {
            instance.setRole(InstanceRole::Voice);
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        auto* corner = child<mm::plugin::ScaleCorner>(*editor, "scaleCorner");
        auto* box = child<juce::ComboBox>(*editor, "zoom");
        REQUIRE((corner != nullptr && box != nullptr));
        const int width = editor->getWidth();
        const int height = editor->getHeight();

        // the grip lies in the bottom right corner, in front of everything, on no other control
        CHECK(corner->isVisible());
        CHECK(corner->getRight() == width);
        CHECK(corner->getBottom() == height);
        CHECK(editor->getChildren().getLast() == corner);
        for (auto* other : editor->getChildren()) {
            if (other != corner && other->isVisible() && other->getWidth() > 0) {
                INFO(other->getComponentID().toStdString());
                CHECK_FALSE(corner->getBounds().intersects(other->getBounds()));
            }
        }

        // 100 % of 1000 (hub) or 600 (voice) px: a quarter of the width to the right is 125 %
        corner->onDragStart();
        corner->onDrag(width / 4.0);
        CHECK(instance.processor.uiScalePct() == 125);
        CHECK(editor->getTransform().mat00 == Catch::Approx(1.25f));
        CHECK(box->getSelectedId() == 125); // on a step: the entry
        CHECK(editor->getWidth() == width); // the layout keeps its size
        CHECK(editor->getHeight() == height);

        // between two steps: the box shows the size as text, with no entry selected
        corner->onDragStart();
        corner->onDrag(width * 0.137);
        CHECK(instance.processor.uiScalePct() == 139); // 125 % of the width on screen is 1.25 * width
        CHECK(box->getSelectedId() == 0);
        CHECK(box->getText() == "Größe 139 %");
        CHECK(editor->getTransform().mat00 == Catch::Approx(1.39f));

        // the drag is relative to the size at its start, and limited to 75 to 200 %
        corner->onDragStart();
        corner->onDrag(-100000.0);
        CHECK(instance.processor.uiScalePct() == 75);
        CHECK(box->getText() == "Größe 75 %");
        corner->onDragStart();
        corner->onDrag(100000.0);
        CHECK(instance.processor.uiScalePct() == 200);
        CHECK(editor->getTransform().mat00 == Catch::Approx(2.0f));

        // a size set from the box afterwards works again
        box->setSelectedId(100, juce::sendNotificationSync);
        CHECK(instance.processor.uiScalePct() == 100);
        CHECK(editor->getTransform().isIdentity());

        // the size between two steps is saved and comes back
        corner->onDragStart();
        corner->onDrag(width * 0.31);
        const int pct = instance.processor.uiScalePct();
        CHECK(pct == 131);
        juce::MemoryBlock saved;
        instance.processor.getStateInformation(saved);
        Instance target("Target");
        target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        CHECK(target.processor.uiScalePct() == 131);
        std::unique_ptr<juce::AudioProcessorEditor> reopened(target.processor.createEditor());
        CHECK(child<juce::ComboBox>(*reopened, "zoom")->getText() == "Größe 131 %");
        CHECK(reopened->getTransform().mat00 == Catch::Approx(1.31f));
    }
}

TEST_CASE("a mouse drag on the corner is measured on screen", "[group-wiring][editor][scale]") {
    Quiet quiet;
    Instance instance("Window");
    std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
    auto* corner = child<mm::plugin::ScaleCorner>(*editor, "scaleCorner");
    REQUIRE(corner != nullptr);
    // an event with the mouse at a point on screen (the window grows under the mouse, so the local position changes)
    const auto at = [&](juce::Point<int> screen) {
        const auto local = corner->getLocalPoint(nullptr, screen).toFloat();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), local, juce::ModifierKeys(), 1.0f,
                                juce::MouseInputSource::defaultOrientation, 0.0f, 0.0f, 0.0f, corner, corner,
                                juce::Time::getCurrentTime(), local, juce::Time::getCurrentTime(), 1, true);
    };
    auto start = corner->localPointToGlobal(juce::Point<int>(8, 8));
    corner->mouseDown(at(start));
    corner->mouseDrag(at(start + juce::Point<int>(100, 30))); // only the width counts
    CHECK(instance.processor.uiScalePct() == 110);
    corner->mouseDrag(at(start + juce::Point<int>(-100, 500)));
    CHECK(instance.processor.uiScalePct() == 90); // relative to the press, though the window changed under the mouse
    corner->mouseDrag(at(start));
    CHECK(instance.processor.uiScalePct() == 100);

    // with the window at 150 % it is 1500 px wide on screen: 60 px more are 4 % more, 150 % becomes 156 %
    instance.processor.setUiScalePct(150);
    editor->setScaleFactor(1.5f);
    start = corner->localPointToGlobal(juce::Point<int>(8, 8));
    corner->mouseDown(at(start));
    corner->mouseDrag(at(start + juce::Point<int>(60, 0)));
    CHECK(instance.processor.uiScalePct() == 156);
}

TEST_CASE("the Help button is in the hub and in the voice window and opens nothing else", "[group-wiring][editor]") {
    Quiet quiet;
    for (const bool voice : {false, true}) {
        INFO((voice ? "voice" : "hub"));
        Instance instance("Window");
        if (voice) {
            instance.setRole(InstanceRole::Voice);
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        auto* help = child<juce::TextButton>(*editor, "help");
        REQUIRE(help != nullptr);
        CHECK(help->isVisible());
        CHECK(help->getButtonText() == "Hilfe");
        CHECK(help->getHeight() == child<juce::ComboBox>(*editor, "zoom")->getHeight());
        CHECK(editor->getLocalBounds().contains(help->getBounds()));
        for (auto* other : editor->getChildren()) {
            if (other != help && other->isVisible() && other->getWidth() > 0) {
                INFO(other->getComponentID().toStdString());
                CHECK_FALSE(help->getBounds().intersects(other->getBounds()));
            }
        }
    }
}

namespace {

/// The playhead clock under the control of the test: the age of a reading no longer depends on how fast the machine
/// is (a loaded CI runner took more than the 0.25 s a reading may be old).
struct FakePlayheadClock {
    FakePlayheadClock() {
        seconds().store(1000.0);
        mm::plugin::ProcessorBase::setPlayheadClock(&read);
    }
    ~FakePlayheadClock() { mm::plugin::ProcessorBase::setPlayheadClock(nullptr); }
    static std::atomic<double>& seconds() {
        static std::atomic<double> value{1000.0};
        return value;
    }
    static double read() { return seconds().load(); }
    static void advance(double by) { seconds().store(seconds().load() + by); }
};

} // namespace

TEST_CASE("the processor reports the host position as long as the transport plays and blocks keep coming",
          "[group-wiring][playhead]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance instance("Head");
    CHECK_FALSE(instance.processor.playheadPpq().has_value()); // nothing processed yet
    instance.setPpq(8.0);
    instance.step();
    auto position = instance.processor.playheadPpq();
    REQUIRE(position.has_value());
    CHECK(*position == Catch::Approx(8.0)); // read in the instant of the block
    FakePlayheadClock::advance(0.1);
    CHECK(*instance.processor.playheadPpq() == Catch::Approx(8.2)); // 120 BPM: the tempo runs it forward

    instance.setPlaying(false);
    instance.step();
    CHECK_FALSE(instance.processor.playheadPpq().has_value());

    instance.setPlaying(true);
    instance.setPpq(5.0);
    instance.step();
    REQUIRE(instance.processor.playheadPpq().has_value());
    juce::AudioBuffer<float> audio(2, 480);
    juce::MidiBuffer midi;
    instance.processor.processBlockBypassed(audio, midi); // bypassed: no playhead
    CHECK_FALSE(instance.processor.playheadPpq().has_value());

    instance.setPlaying(true);
    instance.setPpq(-2.0); // a count-in before the song start
    instance.step();
    CHECK_FALSE(instance.processor.playheadPpq().has_value());

    instance.setPpq(3.0);
    instance.step();
    REQUIRE(instance.processor.playheadPpq().has_value());
    FakePlayheadClock::advance(0.4); // no block for a while: the instance does not process
    CHECK_FALSE(instance.processor.playheadPpq().has_value());
}

TEST_CASE("the piano roll rows of the hub show the playhead in the playing slot and hide it when the transport stops",
          "[group-wiring][editor][playhead]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, marked(36))); }); // one bar, slot 1
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    hub.setPpq(1.0);
    const auto rolls = [&] {
        std::vector<mm::plugin::EditableRoll*> found;
        for (auto* component : editor->getChildren()) {
            if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                found.push_back(&row->roll());
            }
        }
        return found;
    };
    const auto drive = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            hub.step();
            pumpMessages(20);
        }
    };
    drive(5);
    REQUIRE(waitFor([&] { hub.step(); return !rolls().empty() && rolls()[0]->patternTicks() == 3840; }));
    drive(6);
    for (auto* roll : rolls()) {
        // the position of the last block, read in the instant it was written (the block started 0.02 PPQ ago); no
        // further block comes, so the editor's timer ends up exactly there
        const double expected = std::fmod((hub.ppq() - 0.02) * 960.0, 3840.0);
        CHECK(waitFor([&] { return roll->playheadTick() && std::abs(*roll->playheadTick() - expected) <= 2.0; }));
    }

    // another slot is playing: no line in the rows of slot 1
    hub.setSlotParameter(2);
    hub.setPpq(0.0);
    drive(3);
    hub.setPpq(8.0); // a jump: the new slot takes over at once, it is empty
    drive(4);
    for (auto* roll : rolls()) {
        CHECK_FALSE(roll->playheadTick().has_value());
    }

    hub.setSlotParameter(1);
    hub.setPpq(1.0);
    drive(8);
    REQUIRE(waitFor([&] { return rolls()[0]->playheadTick().has_value(); })); // the editor's timer is not instant
    hub.setPlaying(false);
    hub.step();
    REQUIRE(waitFor([&] { return !rolls()[0]->playheadTick().has_value(); }, 2000));
}

TEST_CASE("the roll of the voice window shows the playhead too", "[group-wiring][editor][playhead]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    voice.processor.editSlots([](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, marked(36))); });
    voice.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* roll = child<mm::plugin::RollView>(*editor, "roll");
    REQUIRE(roll != nullptr);
    voice.setPpq(2.0); // half of the one-bar pattern
    REQUIRE(waitFor([&] {
        voice.step();
        return roll->playheadTick().has_value();
    }));
    for (int i = 0; i < 8; ++i) {
        voice.step();
        pumpMessages(20);
    }
    const double expected = std::fmod((voice.ppq() - 0.02) * 960.0, 3840.0);
    CHECK(waitFor([&] { return roll->playheadTick() && std::abs(*roll->playheadTick() - expected) <= 2.0; }));
    voice.setPlaying(false);
    voice.step();
    CHECK(waitFor([&] { return !roll->playheadTick().has_value(); }, 2000));
}

TEST_CASE("the voice window sits on one grid: margins, one control height, no overlap", "[group-wiring][editor][layout]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    REQUIRE(editor->getWidth() == 600);
    REQUIRE(editor->getHeight() == 320);
    const auto inner = editor->getLocalBounds().reduced(16, 0);
    std::vector<juce::Component*> shown;
    for (auto* component : editor->getChildren()) {
        if (component->isVisible() && component->getWidth() > 0) {
            shown.push_back(component);
        }
    }
    REQUIRE(shown.size() >= 8);
    for (auto* component : shown) {
        INFO(component->getComponentID().toStdString());
        if (component->getComponentID() != "scaleCorner") { // the corner grip sits in the margin
            CHECK(inner.contains(component->getBounds())); // 16 px at the sides
        }
        // the controls share one height; the roll, the status line and the drag handle are not controls
        const bool control = dynamic_cast<juce::ComboBox*>(component) != nullptr ||
                             dynamic_cast<juce::TextButton*>(component) != nullptr ||
                             dynamic_cast<juce::ToggleButton*>(component) != nullptr;
        if (control) {
            CHECK(component->getHeight() == 34);
        }
    }
    for (size_t i = 0; i < shown.size(); ++i) {
        for (size_t j = i + 1; j < shown.size(); ++j) {
            INFO(shown[i]->getComponentID().toStdString() << " / " << shown[j]->getComponentID().toStdString());
            CHECK_FALSE(shown[i]->getBounds().intersects(shown[j]->getBounds()));
        }
    }
    // rows line up: the controls of one row have the same top edge, the first column starts at the margin
    auto* role = child<juce::ComboBox>(*editor, "role");
    auto* mute = child<juce::ToggleButton>(*editor, "mute");
    auto* zoom = child<juce::ComboBox>(*editor, "zoom");
    REQUIRE((role != nullptr && mute != nullptr && zoom != nullptr));
    CHECK(role->getX() == 16);
    CHECK(mute->getX() == 16);
    CHECK(zoom->getRight() == 600 - 16);
    auto* help = child<juce::TextButton>(*editor, "help");
    REQUIRE(help != nullptr);
    CHECK(help->getY() == zoom->getY()); // help and size share the footer row
    CHECK(help->getRight() <= zoom->getX());
    CHECK(child<juce::TextButton>(*editor, "generate")->getRight() == 600 - 16);
    CHECK(child<juce::TextButton>(*editor, "generate")->getY() == mute->getY());
    CHECK(child<juce::ComboBox>(*editor, "octave")->getY() == mute->getY());
    CHECK(child<juce::TextButton>(*editor, "openHub")->getY() == mute->getY());
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
    CHECK(waitFor([&] { return box->getSelectedId() == 4; }));
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
    REQUIRE(waitFor([&] { return button->isEnabled(); }));
    button->triggerClick(); // asynchronous
    CHECK(waitFor([&] { return hub.processor.frontRequests() == 1; }));
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
    CHECK(key->getText() == "Tonart: A");
    CHECK(scale->getText() == "Skala: Natürlich Moll");
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
    CHECK(waitFor([&] { return !solo.processor.instanceSettings().generation.seed.has_value(); }));
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
    REQUIRE(waitFor([&] { return child<juce::Slider>(*editor, "energy")->getValue() == 90.0; }));
    CHECK(child<juce::ComboBox>(*editor, "key")->getText() == "Tonart: G");
    CHECK(child<juce::ComboBox>(*editor, "scale")->getText() == "Skala: Dorisch");
    CHECK(child<juce::ComboBox>(*editor, "bars")->getSelectedId() == 8);
    CHECK(child<juce::TextEditor>(*editor, "seed")->getText() == "123");
    CHECK(child<juce::Slider>(*editor, "energy")->getValue() == 90.0);
}

TEST_CASE("the slot strip selects the slot and shows which slots are filled", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(2, marked(41)); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    auto* first = child<juce::TextButton>(*editor, "slot1");
    auto* third = child<juce::TextButton>(*editor, "slot3");
    auto* last = child<juce::TextButton>(*editor, "slot16");
    REQUIRE(first != nullptr);
    REQUIRE(third != nullptr);
    REQUIRE(last != nullptr);
    CHECK(waitFor([&] { return first->getToggleState(); })); // slot 1 is the selected one
    CHECK_FALSE(third->getToggleState());
    const auto colour = [](juce::TextButton* button) { return button->findColour(juce::TextButton::buttonColourId); };
    CHECK(colour(third) != colour(first)); // filled and empty look different
    CHECK(colour(first) == colour(last));

    third->triggerClick();
    CHECK(waitFor([&] { return solo.processor.activeSlot() == 3; }));
    CHECK(waitFor([&] { return third->getToggleState() && !first->getToggleState(); }));
    for (int slot = 1; slot <= 16; ++slot) {
        CHECK(child<juce::TextButton>(*editor, "slot" + juce::String(slot)) != nullptr);
    }
}

TEST_CASE("the hub UI has a row per voice of the playing slot", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    // An empty slot: the two default voices, without notes.
    REQUIRE(find(*editor, "row_1") != nullptr);
    REQUIRE(find(*editor, "row_2") != nullptr);
    CHECK(find(*editor, "row_3") == nullptr);
    CHECK(dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_1"))->layout().notes.empty());

    solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(41, 71)); });
    REQUIRE(
        waitFor([&] { return !dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_2"))->layout().notes.empty(); }));
    auto* bass = dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_1"));
    auto* melody = dynamic_cast<mm::plugin::RollView*>(find(*editor, "roll_2"));
    REQUIRE(bass != nullptr);
    REQUIRE(melody != nullptr);
    CHECK_FALSE(bass->layout().notes.empty());
    CHECK_FALSE(melody->layout().notes.empty());
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "name_1"))->getText() == "Bass");
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "name_2"))->getText() == "Melodie");
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
    REQUIRE(waitFor([&] { return find(*editor, "row_3") != nullptr; }));
    CHECK(dynamic_cast<juce::Label*>(find(*editor, "name_3"))->getText() == "Stimme 3");

    // Back to a slot with two voices: two rows again.
    solo.setSlotParameter(1);
    runTo({&solo}, 8.1);
    CHECK(waitFor([&] { return find(*editor, "row_3") == nullptr; }));
}

TEST_CASE("each voice row has its own Mute and its own grip", "[group-wiring][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.setResult(0, markedTwoVoices(41, 71)); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    auto* mute2 = child<juce::ToggleButton>(*editor, "mute_2");
    REQUIRE(mute2 != nullptr);
    mute2->setToggleState(true, juce::sendNotificationSync);
    CHECK(solo.processor.parameters().getRawParameterValue("mute_2")->load() == 1.0f);
    CHECK(solo.processor.parameters().getRawParameterValue("mute_1")->load() == 0.0f);
    solo.setMute(1, true); // automation of the other voice moves its button only
    CHECK(waitFor([&] { return child<juce::ToggleButton>(*editor, "mute_1")->getToggleState(); }));
    solo.setMute(2, false);
    CHECK(waitFor([&] { return !mute2->getToggleState(); }));

    auto* grip1 = dynamic_cast<mm::plugin::DragHandle*>(find(*editor, "drag_1"));
    auto* grip2 = dynamic_cast<mm::plugin::DragHandle*>(find(*editor, "drag_2"));
    REQUIRE(grip1 != nullptr);
    REQUIRE(grip2 != nullptr);
    waitFor([&] { return grip1->file() != juce::File() && grip2->file() != juce::File(); });
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
    auto* info = dynamic_cast<juce::Label*>(find(*editor, "info"));
    REQUIRE(info != nullptr);
    CHECK(info->getText().contains("leer"));
    mm::core::Pattern pattern = marked(41);
    pattern.context.root = 2;
    pattern.context.scaleId = "dorian";
    pattern.info.seed = 777;
    pattern.info.winnerSeed = 888;
    solo.processor.editSlots([&](mm::core::SlotBank& bank) { bank.setResult(0, pattern); });
    REQUIRE(waitFor([&] { return info->getText().contains("Sieger 888"); }));
    CHECK(info->getText().contains("Slot 1"));
    CHECK(info->getText().contains("D Dorisch"));
    CHECK(info->getText().contains("Seed 777"));
    CHECK(info->getText().contains("Sieger 888"));
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
    waitFor([&] { return !solo.processor.slotsSnapshot().isEmpty(0); });
    const auto bank = solo.processor.slotsSnapshot();
    REQUIRE_FALSE(bank.isEmpty(0));
    const auto& pattern = *bank.slot(0)->pattern;
    CHECK(pattern.context.root == 2);
    CHECK(pattern.context.scaleId == std::string(scales[2].id));
    CHECK(pattern.lengthBars == 2);
    CHECK(pattern.info.seed == 77);
    CHECK(waitFor([&] { return dynamic_cast<juce::Label*>(find(*editor, "info"))->getText().contains("Seed 77"); }));
}

TEST_CASE("the editor shows the German texts of the table", "[group-wiring][editor][i18n]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    CHECK(child<juce::ComboBox>(*editor, "key")->getItemText(0) == "Tonart: auto");
    CHECK(child<juce::ComboBox>(*editor, "bars")->getItemText(0) == "1 Takt");
    CHECK(child<juce::ComboBox>(*editor, "bars")->getItemText(1) == "2 Takte");
    CHECK(child<juce::ComboBox>(*editor, "octave")->getItemText(3) == "Oktave +1");
    CHECK(child<juce::TextEditor>(*editor, "seed")->getTextToShowWhenEmpty() == "Seed: zufällig");
    CHECK(child<juce::Button>(*editor, "random")->getButtonText() == "Zufälliger Seed");
    CHECK(child<juce::Button>(*editor, "mute_1")->getButtonText() == "Stumm");
}

TEST_CASE("a note edit in the hub plays at once there and at the next bar line in the voice", "[group-wiring][edit]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    runTo({&hub, &voice}, 1.0);
    REQUIRE(hub.processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 60, 3 * 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    runTo({&hub, &voice}, 9.0);
    CHECK(near(hub.firstWith(60), at(3.0)));   // in the bar that plays
    CHECK(near(voice.firstWith(60), at(7.0))); // the voice takes the slots at the bar line
    CHECK(voice.processor.slotsSnapshot().slot(0)->pattern->voices[0].notes.size() == 2);
}

TEST_CASE("an undo in the hub reaches the voice, a voice cannot undo", "[group-wiring][edit]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    runTo({&hub, &voice}, 1.0);
    REQUIRE(hub.processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::addNote(pattern, 0, 60, 3 * 960, 240, 100) != 0 ? size_t{1} : size_t{0};
    }));
    runTo({&hub, &voice}, 5.0);
    REQUIRE(hub.processor.undo());
    CHECK_FALSE(voice.processor.undo());
    runTo({&hub, &voice}, 13.0);
    CHECK(voice.processor.slotsSnapshot().slot(0)->pattern->voices[0].notes.size() == 1);
    CHECK(voice.firstWith(60, at(8.0)) < 0);
    CHECK(hub.firstWith(60, at(8.0)) < 0);
}

TEST_CASE("the lock switch of a voice row follows the pattern and locks the voice", "[group-wiring][editor][lock]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    auto* lock1 = dynamic_cast<juce::Button*>(find(*editor, "lock_1"));
    auto* lock2 = dynamic_cast<juce::Button*>(find(*editor, "lock_2"));
    REQUIRE((lock1 != nullptr && lock2 != nullptr));
    CHECK(lock1->getButtonText() == "Sperren");
    CHECK_FALSE(lock1->isEnabled()); // an empty slot has nothing to lock

    solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, markedTwoVoices(41, 42))); });
    REQUIRE(waitFor([&] { return lock1->isEnabled(); }));
    CHECK_FALSE(lock1->getToggleState());

    lock2->triggerClick();
    REQUIRE(waitFor([&] { return solo.processor.playingVoices()[1].locked; }));
    CHECK_FALSE(solo.processor.playingVoices()[0].locked);
    REQUIRE(waitFor([&] { return lock2->getToggleState(); }));
    CHECK_FALSE(lock1->getToggleState());

    // From outside (undo): the switch follows.
    REQUIRE(solo.processor.undo());
    REQUIRE(waitFor([&] { return !lock2->getToggleState(); }));
    lock2->triggerClick();
    REQUIRE(waitFor([&] { return solo.processor.playingVoices()[1].locked; }));
    lock2->triggerClick(); // and off again
    REQUIRE(waitFor([&] { return !solo.processor.playingVoices()[1].locked; }));

    solo.processor.editSlots([&](mm::core::SlotBank& bank) { bank.clear(0); }); // the slot is empty again
    REQUIRE(waitFor([&] { return !lock1->isEnabled(); }));
    CHECK_FALSE(lock2->isEnabled());
}

namespace {

/// A solo instance with a two-bar pattern and a piano roll on its melody voice, driven through the gesture methods.
/// `refresh` plays the part of the editor's timer: it hands the current voice to the roll.
struct RollRig {
    RollRig() : roll(solo.processor, 2) {
        roll.setBounds(0, 0, 800, 200);
        mm::core::Pattern pattern = mm::core::makeEmptyPattern(2, "peak_time"); // A minor
        for (const auto& note :
             std::vector<std::array<uint32_t, 3>>{{60, 0, 480}, {64, 960, 480}, {67, 1920, 480}, {69, 3840, 960}}) {
            REQUIRE(mm::core::addNote(pattern, 1, static_cast<uint8_t>(note[0]), note[1], note[2], 100) != 0);
        }
        solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, std::move(pattern))); });
        refresh();
    }

    void refresh() {
        const auto views = solo.processor.playingVoices();
        REQUIRE(views.size() >= 2);
        roll.setSource(&views[1]);
    }
    const mm::core::RollNote& note(size_t index) const { return roll.source().at(index); }
    uint32_t idAt(size_t index) const { return note(index).id; }
    double xOf(uint32_t tick, double inside = 3.0) const { return roll.geometry().tickToX(tick) + inside; }
    double yOf(int pitch) const { return roll.geometry().pitchToY(pitch) + roll.geometry().rowHeight() / 2.0; }
    juce::Point<double> at(uint32_t tick, int pitch, double inside = 3.0) const {
        return {xOf(tick, inside), yOf(pitch)};
    }
    double pixelsForTicks(double ticks) const { return ticks / roll.geometry().ticksPerPixel(); }
    double pixelsForRows(double rows) const { return rows * roll.geometry().rowHeight(); }
    /// A drag from `from` by (dx, dy) in three steps.
    void dragBy(juce::Point<double> from, double dx, double dy) {
        roll.press(from, {});
        for (const double share : {0.3, 0.7, 1.0}) {
            roll.drag({from.x + dx * share, from.y + dy * share});
            refresh();
        }
        roll.release({from.x + dx, from.y + dy});
        refresh();
    }
    std::vector<mm::core::Note> melody() const {
        return solo.processor.slotsSnapshot().slot(0)->pattern->voices[1].notes;
    }

    Quiet quiet;
    Instance solo{"Solo"};
    mm::plugin::EditableRoll roll;
};

} // namespace

TEST_CASE("the roll shows the source notes of its voice, not the rendered ones", "[roll][editor]") {
    RollRig rig;
    REQUIRE(rig.roll.source().size() == 4);
    CHECK(rig.note(0).pitch == 60);
    CHECK(rig.note(1).startTick == 960);
    CHECK(rig.note(3).lengthTicks == 960);
    CHECK(rig.idAt(0) != 0);
    CHECK(rig.roll.selection().empty());
    // the window fits the two bars; the pitch rows are centred on the notes
    CHECK(rig.roll.viewport().startTick == 0);
    CHECK(rig.roll.viewport().spanTicks == 2 * mm::core::kTicksPerBar);
    const auto& v = rig.roll.viewport();
    CHECK(v.lowPitch <= 60);
    CHECK(v.lowPitch + v.rows > 69);
}

TEST_CASE("a click selects, shift adds, shift on a selected note removes, a click in the empty clears",
          "[roll][editor]") {
    RollRig rig;
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(0)});

    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    CHECK(rig.roll.selection() == (std::set<uint32_t>{rig.idAt(0), rig.idAt(1)}));

    rig.roll.press(rig.at(0, 60), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(0, 60));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});

    rig.roll.press(rig.at(1200, 70), {}); // nothing there
    rig.roll.release(rig.at(1200, 70));
    CHECK(rig.roll.selection().empty());
    CHECK_FALSE(rig.solo.processor.canUndo()); // selecting is no edit
}

TEST_CASE("a click on one of several selected notes narrows the selection, a drag moves them all", "[roll][editor]") {
    RollRig rig;
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    REQUIRE(rig.roll.selection().size() == 2);

    // a plain click on a selected note keeps the other until the mouse comes up, then narrows
    rig.roll.press(rig.at(960, 64), {});
    CHECK(rig.roll.selection().size() == 2);
    rig.roll.release(rig.at(960, 64));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});

    // a shake of the mouse below the drag threshold is still a click
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    REQUIRE(rig.roll.selection().size() == 2);
    rig.roll.press(rig.at(960, 64), {});
    rig.roll.drag({rig.at(960, 64).x + 2.0, rig.at(960, 64).y});
    rig.roll.release(rig.at(960, 64));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});
    CHECK_FALSE(rig.solo.processor.canUndo());

    // with both selected, dragging one carries the other
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    rig.dragBy(rig.at(960, 64), rig.pixelsForTicks(480.0), 0.0);
    const auto notes = rig.melody();
    CHECK(notes[0].startTick == 480);
    CHECK(notes[1].startTick == 1440);
    CHECK(notes[2].startTick == 1920); // not selected
}

TEST_CASE("the rubber band selects, with shift it adds", "[roll][editor]") {
    RollRig rig;
    const auto from = juce::Point<double>(rig.xOf(900, 0.0), rig.yOf(70));
    const auto to = juce::Point<double>(rig.xOf(2500, 0.0), rig.yOf(58));
    rig.roll.press(from, {});
    rig.roll.drag(to);
    rig.roll.release(to);
    CHECK(rig.roll.selection() == (std::set<uint32_t>{rig.idAt(1), rig.idAt(2)}));

    rig.roll.press({rig.xOf(3800, 0.0), rig.yOf(72)}, juce::ModifierKeys::shiftModifier);
    rig.roll.drag({rig.xOf(4900, 0.0), rig.yOf(66)});
    rig.roll.release({rig.xOf(4900, 0.0), rig.yOf(66)});
    CHECK(rig.roll.selection() == (std::set<uint32_t>{rig.idAt(1), rig.idAt(2), rig.idAt(3)}));
    CHECK_FALSE(rig.solo.processor.canUndo());
}

TEST_CASE("a double click adds a note on the grid and the scale, and one undo takes it away", "[roll][editor]") {
    RollRig rig;
    // tick 1250 is in the 16th at 1200, row 61 (C#) snaps down to C
    rig.roll.doubleClick({rig.xOf(1250, 0.0), rig.yOf(61)});
    rig.refresh();
    REQUIRE(rig.roll.source().size() == 5);
    const auto added = std::find_if(rig.roll.source().begin(), rig.roll.source().end(),
                                    [](const auto& n) { return n.startTick == 1200; });
    REQUIRE(added != rig.roll.source().end());
    CHECK(added->pitch == 60);
    CHECK(added->lengthTicks == 240);
    CHECK(added->velocity == 100);
    CHECK(rig.roll.selection() == std::set<uint32_t>{added->id});
    CHECK(rig.solo.processor.canUndo());
    REQUIRE(rig.solo.processor.undo());
    rig.refresh();
    CHECK(rig.roll.source().size() == 4);
    CHECK(rig.roll.selection().empty()); // the new note is gone, so is its selection
}

TEST_CASE("the grid and the snapping of the roll decide where a double click lands", "[roll][editor]") {
    RollRig rig;
    rig.roll.setEditSettings({8, false}, mm::core::PitchSnap::Chromatic);
    rig.roll.doubleClick({rig.xOf(1250, 0.0), rig.yOf(61)});
    rig.refresh();
    auto it = std::find_if(rig.roll.source().begin(), rig.roll.source().end(),
                           [](const auto& n) { return n.startTick == 960 && n.pitch == 61; });
    REQUIRE(it != rig.roll.source().end());
    CHECK(it->lengthTicks == 480);

    rig.roll.setEditSettings({16, true}, mm::core::PitchSnap::Chromatic);
    rig.roll.doubleClick({rig.xOf(2500, 0.0), rig.yOf(62)});
    rig.refresh();
    it = std::find_if(rig.roll.source().begin(), rig.roll.source().end(),
                      [](const auto& n) { return n.startTick == 2400 && n.pitch == 62; });
    REQUIRE(it != rig.roll.source().end()); // 16th triplets: 2500 / 160 = 15.6 -> 2400
    CHECK(it->lengthTicks == 160);
}

TEST_CASE("a double click on a note deletes that note only", "[roll][editor]") {
    RollRig rig;
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    const uint32_t second = rig.idAt(1);
    rig.roll.doubleClick(rig.at(960, 64));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(0)}); // at once, before the view is refreshed
    rig.refresh();
    REQUIRE(rig.roll.source().size() == 3);
    CHECK(std::none_of(rig.roll.source().begin(), rig.roll.source().end(),
                       [&](const auto& n) { return n.id == second; }));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(0)});
    REQUIRE(rig.solo.processor.undo());
    rig.refresh();
    CHECK(rig.roll.source().size() == 4);
}

TEST_CASE("dragging a note moves it by grid and scale and is one undo step", "[roll][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    // the note E4 (64 at 960) goes half a bar later and up three rows: 67 (G) is in the scale
    rig.dragBy(rig.at(960, 64), rig.pixelsForTicks(480.0), -rig.pixelsForRows(3.0));
    auto notes = rig.melody();
    REQUIRE(notes.size() == 4);
    CHECK(notes[1].startTick == 1440);
    CHECK(notes[1].pitch == 67);
    CHECK(notes[0] == before[0]);
    CHECK(notes[2] == before[2]);
    CHECK(rig.roll.selection() == std::set<uint32_t>{before[1].id});

    REQUIRE(rig.solo.processor.undo()); // one step for the whole gesture, however many mouse moves it had
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());
    REQUIRE(rig.solo.processor.redo());
    CHECK(rig.melody()[1].startTick == 1440);
}

TEST_CASE("a drag that snaps back to where it started changes nothing", "[roll][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    rig.dragBy(rig.at(960, 64), rig.pixelsForTicks(60.0), 0.0); // a quarter of a 16th: snaps back
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());
}

TEST_CASE("a note cannot be dragged out of the pattern", "[roll][editor]") {
    RollRig rig;
    rig.dragBy(rig.at(3840, 69), rig.pixelsForTicks(5000.0), -rig.pixelsForRows(100.0));
    const auto notes = rig.melody();
    for (const auto& note : notes) {
        CHECK(note.startTick + note.lengthTicks <= 2 * mm::core::kTicksPerBar);
        CHECK(note.pitch <= 127);
    }
    // the block is rigid: the last note ends at the end of the pattern, the others moved with it only when selected
    CHECK(notes[3].startTick + notes[3].lengthTicks == 2 * mm::core::kTicksPerBar);
}

TEST_CASE("dragging the right edge sets the length from the snapped end and is one undo step", "[roll][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    const auto edge = rig.roll.geometry().tickToX(960 + 480) - 1.0; // the last pixel of the note
    rig.dragBy({edge, rig.yOf(64)}, rig.pixelsForTicks(480.0), 0.0);
    auto notes = rig.melody();
    CHECK(notes[1].lengthTicks == 960);
    CHECK(notes[1].startTick == 960);
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody() == before);

    // dragged to nothing: one grid step is the least
    rig.refresh();
    rig.dragBy({edge, rig.yOf(64)}, -rig.pixelsForTicks(2000.0), 0.0);
    CHECK(rig.melody()[1].lengthTicks == 240);
}

TEST_CASE("a drag is cancelled when somebody else changes the note under it", "[roll][editor]") {
    RollRig rig;
    const auto from = rig.at(960, 64);
    rig.roll.press(from, {});
    rig.roll.drag({from.x + rig.pixelsForTicks(480.0), from.y});
    rig.refresh();
    REQUIRE(rig.melody()[1].startTick == 1440);
    // another edit moves the note (an undo or the hub would do the same)
    REQUIRE(rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) {
        const std::vector<uint32_t> ids{rig.idAt(1)};
        return mm::core::moveNotes(p, 1, ids, 240, 0);
    }));
    rig.refresh();
    const auto externallyMoved = rig.melody();
    rig.roll.drag({from.x + rig.pixelsForTicks(960.0), from.y});
    CHECK_FALSE(rig.roll.gestureActive());
    CHECK(rig.melody() == externallyMoved); // the drag did not overwrite it
    rig.roll.release({from.x, from.y});
}

TEST_CASE("another slot or length starts a fresh view and drops the selection", "[roll][editor]") {
    RollRig rig;
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    REQUIRE_FALSE(rig.roll.selection().empty());
    rig.roll.scrollBy(0, 5);
    const auto scrolled = rig.roll.viewport();
    // the same pattern edited elsewhere keeps selection and window
    REQUIRE(rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) {
        const std::vector<uint32_t> ids{rig.idAt(2)};
        return mm::core::moveNotes(p, 1, ids, 240, 0);
    }));
    rig.refresh();
    CHECK_FALSE(rig.roll.selection().empty());
    CHECK(rig.roll.viewport() == scrolled);
    // an empty slot empties the roll
    rig.solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.clear(0); });
    const auto views = rig.solo.processor.playingVoices();
    rig.roll.setSource(views.empty() ? nullptr : &views[1]);
    CHECK(rig.roll.source().empty());
    CHECK(rig.roll.selection().empty());
    rig.roll.doubleClick({100.0, 50.0}); // and nothing can be added
    CHECK(rig.solo.processor.slotsSnapshot().slot(0)->pattern == std::nullopt);
}

TEST_CASE("select all, delete and moving the selection by a step are actions of the roll", "[roll][editor]") {
    RollRig rig;
    rig.roll.selectAll();
    CHECK(rig.roll.selection().size() == 4);

    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    REQUIRE(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});

    rig.roll.moveSelection(240);
    CHECK(rig.melody()[1].startTick == 1200);
    rig.roll.moveSelection(-960);
    CHECK(rig.melody()[1].startTick == 240);
    rig.refresh();
    rig.roll.moveSelection(0, 1); // E -> F in A minor
    CHECK(rig.melody()[1].pitch == 65);
    rig.roll.moveSelection(0, -1);
    CHECK(rig.melody()[1].pitch == 64);
    rig.roll.moveSelection(0, 1, true); // an octave
    CHECK(rig.melody()[1].pitch == 76);
    rig.roll.moveSelection(0, -1, true);
    CHECK(rig.melody()[1].pitch == 64);

    REQUIRE(rig.solo.processor.undo()); // each is one step
    CHECK(rig.melody()[1].pitch == 76);
    REQUIRE(rig.solo.processor.redo());
    CHECK(rig.melody()[1].pitch == 64);

    rig.refresh();
    rig.roll.deleteSelection();
    rig.refresh();
    CHECK(rig.roll.source().size() == 3);
    CHECK(rig.roll.selection().empty());
}

TEST_CASE("the roll takes no keys: D-126 gives MidiMaid no predefined shortcuts", "[roll][editor]") {
    RollRig rig;
    CHECK_FALSE(rig.roll.getWantsKeyboardFocus());
    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    const auto before = rig.melody();
    for (const auto& key : {juce::KeyPress('Z', juce::ModifierKeys::commandModifier, 0),
                            juce::KeyPress('A', juce::ModifierKeys::commandModifier, 0),
                            juce::KeyPress('A', juce::ModifierKeys(), 0), juce::KeyPress('S', juce::ModifierKeys(), 0),
                            juce::KeyPress(juce::KeyPress::deleteKey), juce::KeyPress(juce::KeyPress::backspaceKey),
                            juce::KeyPress(juce::KeyPress::leftKey), juce::KeyPress(juce::KeyPress::rightKey),
                            juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::altModifier, 0)}) {
        CHECK_FALSE(rig.roll.keyPressed(key));
    }
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)}); // nothing selected all, nothing deleted
}

TEST_CASE("zoom and scroll move the window and stay inside the pattern", "[roll][editor]") {
    RollRig rig;
    const auto full = rig.roll.viewport();
    rig.roll.zoomAt(100.0, 0.5);
    CHECK(rig.roll.viewport().spanTicks == full.spanTicks / 2);
    const auto anchorBefore = full.startTick + 100.0 * full.spanTicks / 766.0;
    const auto g = rig.roll.geometry();
    CHECK(std::abs(g.xToTick(100.0) - anchorBefore) < 40.0); // the tick under the mouse stays put
    rig.roll.scrollBy(100000, 100);
    CHECK(rig.roll.viewport().startTick + rig.roll.viewport().spanTicks == 2 * mm::core::kTicksPerBar);
    CHECK(rig.roll.viewport().lowPitch + rig.roll.viewport().rows == 128);
    rig.roll.scrollBy(-100000, -200);
    CHECK(rig.roll.viewport().startTick == 0);
    CHECK(rig.roll.viewport().lowPitch == 0);
    rig.roll.zoomAt(0.0, 100.0);
    CHECK(rig.roll.viewport().spanTicks == 2 * mm::core::kTicksPerBar);
    // the zoom decides what a pixel is: a note stays hittable after zooming
    rig.roll.zoomAt(0.0, 0.25);
    rig.roll.scrollBy(0, 60);
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    CHECK(rig.roll.selection().size() == 1);
}

TEST_CASE("a voice instance cannot edit through the roll", "[roll][editor]") {
    RollRig rig;
    auto settings = rig.solo.processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    rig.solo.processor.setInstanceSettings(settings);
    const auto before = rig.melody();
    rig.roll.doubleClick(rig.at(1200, 62));
    rig.dragBy(rig.at(960, 64), rig.pixelsForTicks(480.0), 0.0);
    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    rig.roll.deleteSelection();
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());
}

TEST_CASE("deleting or moving a sounding note in the roll leaves no hanging note", "[roll][editor][engine]") {
    for (const bool remove : {true, false}) {
        Quiet quiet;
        Instance solo("Solo");
        solo.setRole(InstanceRole::Solo, SlotFollow::Hub, 2); // the melody: the bass is cut before the kick
        auto pattern = mm::core::makeEmptyPattern(1, "peak_time");
        REQUIRE(mm::core::addNote(pattern, 1, 69, 0, 3000, 100) != 0);
        solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, std::move(pattern))); });
        mm::plugin::EditableRoll roll(solo.processor, 2);
        roll.setBounds(0, 0, 800, 200);

        runTo({&solo}, 1.0); // the note has been sounding for one beat
        REQUIRE(solo.notes().size() == 1);
        REQUIRE(solo.noteOffs().empty());
        const auto views = solo.processor.playingVoices();
        roll.setSource(&views[1]);
        const auto g = roll.geometry();
        const juce::Point<double> onNote{g.tickToX(500.0), g.pitchToY(69) + g.rowHeight() / 2.0};
        roll.press(onNote, {});
        roll.release(onNote);
        REQUIRE(roll.selection().size() == 1);
        if (remove) {
            roll.deleteSelection();
        } else {
            roll.moveSelection(240);
        }
        runTo({&solo}, 1.5);
        CHECK(solo.noteOffs().size() == 1); // the sounding note ended at once
        CHECK(solo.noteOffs().front().pitch == 69);
        runTo({&solo}, 4.5); // past the loop: a moved note starts again from its new place at 4.25
        CHECK(solo.notes().size() == (remove ? 1u : 2u));
        CHECK(solo.noteOffs().size() == 1); // the second one is still sounding, nothing else hangs
    }
}

TEST_CASE("the hub UI hands grid, triplets and snapping to the rolls and shows the source notes", "[roll][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    auto* grid = dynamic_cast<juce::ComboBox*>(find(*editor, "grid_box"));
    auto* triplet = dynamic_cast<juce::Button*>(find(*editor, "triplet"));
    auto* snapBox = dynamic_cast<juce::ComboBox*>(find(*editor, "snap_box"));
    REQUIRE((grid != nullptr && triplet != nullptr && snapBox != nullptr));
    // the edit settings belong to the expert level (D-162): switch it on first
    CHECK_FALSE(grid->isVisible());
    dynamic_cast<juce::Button*>(find(*editor, "expert"))->triggerClick();
    REQUIRE(waitFor([&] { return grid->isVisible(); }));
    CHECK(triplet->getButtonText() == "Triolen");
    CHECK(snapBox->getText() == "Einrasten: Skala");

    auto* roll1 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_1"));
    auto* roll2 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_2"));
    REQUIRE((roll1 != nullptr && roll2 != nullptr));
    CHECK(roll2->snap().grid == mm::core::EditGrid{16, false}); // the defaults
    CHECK(roll2->snap().pitch == mm::core::PitchSnap::Scale);

    grid->setSelectedId(8, juce::sendNotificationSync);
    triplet->setToggleState(true, juce::sendNotificationSync);
    snapBox->setSelectedId(2, juce::sendNotificationSync);
    for (auto* roll : {roll1, roll2}) {
        CHECK(roll->snap().grid == mm::core::EditGrid{8, true});
        CHECK(roll->snap().pitch == mm::core::PitchSnap::Chromatic);
    }

    solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, markedTwoVoices(41, 42))); });
    REQUIRE(waitFor([&] { return roll2->source().size() == 1; }));
    CHECK(roll1->source().front().pitch == 41);
    CHECK(roll2->source().front().pitch == 42);
    CHECK(roll2->source().front().lengthTicks == 240); // the source note, not the rendered one

    // the settings survive a new set of rows (a pattern with another number of voices)
    mm::core::Pattern three = markedTwoVoices(41, 42);
    three.voices.push_back(three.voices[1]);
    three.voices.back().midiChannel = 3;
    three.voices.back().notes[0].id = mm::core::allocateNoteId(three);
    solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(1, three)); });
    solo.processor.selectSlot(2);
    auto* roll3 = static_cast<mm::plugin::EditableRoll*>(nullptr);
    REQUIRE(waitFor([&] {
        roll3 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_3"));
        return roll3 != nullptr;
    }));
    CHECK(roll3->snap().grid == mm::core::EditGrid{8, true});
}

TEST_CASE("a resize is cancelled when somebody else changes the length under it", "[roll][editor]") {
    RollRig rig;
    const auto edge = rig.roll.geometry().tickToX(960 + 480) - 1.0;
    const juce::Point<double> from{edge, rig.yOf(64)};
    rig.roll.press(from, {});
    rig.roll.drag({from.x + rig.pixelsForTicks(480.0), from.y});
    rig.refresh();
    REQUIRE(rig.melody()[1].lengthTicks == 960);
    REQUIRE(rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) {
        const std::vector<uint32_t> ids{rig.idAt(1)};
        return mm::core::setLength(p, 1, ids, 120);
    }));
    rig.refresh();
    rig.roll.drag({from.x + rig.pixelsForTicks(960.0), from.y});
    CHECK_FALSE(rig.roll.gestureActive());
    CHECK(rig.melody()[1].lengthTicks == 120); // the drag did not overwrite it
    rig.roll.release(from);
}

namespace {

juce::MouseEvent mouseEventAt(juce::Component& component, juce::Point<float> position, int clicks = 1,
                              juce::ModifierKeys mods = {}) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), position, mods, 0.0f, 0.0f, 0.0f, 0.0f,
                            0.0f, &component, &component, juce::Time::getCurrentTime(), position,
                            juce::Time::getCurrentTime(), clicks, false);
}

} // namespace

TEST_CASE("the mouse handlers of the roll translate into gestures with the key gutter taken off", "[roll][editor]") {
    RollRig rig;
    const auto gutter = static_cast<float>(mm::plugin::EditableRoll::kGutter);
    const auto title = static_cast<float>(mm::plugin::EditableRoll::kTitleHeight); // the name sits above the notes
    const auto onNote = rig.at(960, 64);
    const juce::Point<float> position(static_cast<float>(onNote.x) + gutter, static_cast<float>(onNote.y) + title);

    rig.roll.mouseDown(mouseEventAt(rig.roll, position));
    rig.roll.mouseUp(mouseEventAt(rig.roll, position));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});

    // a drag with the mouse events moves the note
    const auto target = position + juce::Point<float>(static_cast<float>(rig.pixelsForTicks(480.0)), 0.0f);
    rig.roll.mouseDown(mouseEventAt(rig.roll, position));
    rig.roll.mouseDrag(mouseEventAt(rig.roll, target));
    rig.roll.mouseUp(mouseEventAt(rig.roll, target));
    CHECK(rig.melody()[1].startTick == 1440);
    rig.refresh();

    // the second press of a double click deletes the note under it (the first one only selected)
    const auto moved = rig.at(1440, 64);
    const juce::Point<float> second(static_cast<float>(moved.x) + gutter, static_cast<float>(moved.y) + title);
    rig.roll.mouseDown(mouseEventAt(rig.roll, second, 2));
    rig.roll.mouseUp(mouseEventAt(rig.roll, second, 2));
    CHECK(rig.melody().size() == 3);
    rig.refresh();

    // the wheel with Cmd/Ctrl zooms, plain it scrolls
    juce::MouseWheelDetails wheel;
    wheel.deltaY = 0.5f;
    const auto spanBefore = rig.roll.viewport().spanTicks;
    const auto fieldX = static_cast<double>(second.x) - gutter;
    const auto tickUnderMouse = rig.roll.geometry().xToTick(fieldX);
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, second, 1, juce::ModifierKeys::commandModifier), wheel);
    CHECK(rig.roll.viewport().spanTicks < spanBefore);
    // the tick under the mouse stays where it was, so the gutter is taken off the position
    const auto tickAfter = static_cast<double>(rig.roll.geometry().xToTick(fieldX));
    CHECK(std::abs(tickAfter - static_cast<double>(tickUnderMouse)) < 60.0);
    const auto lowBefore = rig.roll.viewport().lowPitch;
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, second), wheel);
    CHECK(rig.roll.viewport().lowPitch > lowBefore);
    // a trackpad: many small steps, none of them a whole row, still scroll (they add up); a change of direction goes
    // back
    juce::MouseWheelDetails small;
    small.deltaY = 0.04f; // 0.32 of a row
    const auto lowSmall = rig.roll.viewport().lowPitch;
    for (int i = 0; i < 20; ++i) {
        rig.roll.mouseWheelMove(mouseEventAt(rig.roll, second), small);
    }
    CHECK(rig.roll.viewport().lowPitch == lowSmall + 6); // 6.4 rows
    small.deltaY = -0.04f;
    for (int i = 0; i < 19; ++i) { // 6.08 rows back
        rig.roll.mouseWheelMove(mouseEventAt(rig.roll, second), small);
    }
    CHECK(rig.roll.viewport().lowPitch ==
          lowSmall); // the leftover 0.4 of the first direction did not count (it would leave one row)
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, second, 1, juce::ModifierKeys::shiftModifier), wheel);
    CHECK(rig.roll.viewport().startTick >= 0);
    rig.roll.selectAll();
    CHECK(rig.roll.selection().size() == 3);
}

namespace {

constexpr double kLane = mm::plugin::EditableRoll::kLaneHeight;

/// The lane height that stands for `velocity`: the inverse of what the lane reads from the mouse.
double laneY(int velocity) {
    return kLane * (1.0 - static_cast<double>(velocity) / 127.0);
}

/// Sets the velocities of the melody notes (by index) through the processor, like an earlier edit would.
void setMelodyVelocities(RollRig& rig, const std::vector<std::pair<size_t, uint8_t>>& values) {
    std::vector<mm::core::VelocityChange> changes;
    for (const auto& [index, velocity] : values) {
        changes.push_back({rig.idAt(index), velocity});
    }
    REQUIRE(
        rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) { return mm::core::setVelocities(p, 1, changes); }));
    rig.refresh();
}

} // namespace

TEST_CASE("dragging a velocity bar sets that velocity and is one undo step", "[roll][lane][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    const juce::Point<double> from{rig.xOf(960, 2.0), laneY(100)};
    rig.roll.pressLane(from, {});
    for (const double target : {90.0, 70.0, 50.0}) {
        rig.roll.drag({from.x, laneY(static_cast<int>(target))});
        rig.refresh();
    }
    CHECK(rig.roll.velocityHint() == 50);
    rig.roll.release({from.x, laneY(50)});
    rig.refresh();
    CHECK(rig.melody()[1].velocity == 50);
    CHECK(rig.melody()[0].velocity == before[0].velocity);
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)}); // the bar selected its note
    CHECK(rig.roll.velocityHint() == 0);                            // the number goes with the gesture
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());
}

TEST_CASE("a click on a bar only selects, a small shake does not change the velocity", "[roll][lane][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    const juce::Point<double> from{rig.xOf(960, 2.0), laneY(40)};
    rig.roll.pressLane(from, {});
    rig.roll.drag({from.x, from.y + 2.0});
    rig.roll.release(from);
    CHECK(rig.melody() == before);
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});
    CHECK_FALSE(rig.solo.processor.canUndo());
}

TEST_CASE("a selection moves by the same velocity delta and stays inside 1 to 127", "[roll][lane][editor]") {
    RollRig rig;
    setMelodyVelocities(rig, {{0, 100}, {1, 60}});
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    REQUIRE(rig.roll.selection().size() == 2);

    // 100 -> 80: the other one follows by -20
    const juce::Point<double> from{rig.xOf(0, 2.0), laneY(100)};
    rig.roll.pressLane(from, {});
    rig.roll.drag({from.x, laneY(80)});
    rig.refresh();
    CHECK(rig.melody()[0].velocity == 80);
    CHECK(rig.melody()[1].velocity == 40);
    CHECK(rig.melody()[2].velocity == 100); // not selected
    // further down than the lower one can follow: it stops at 1, the order of the two stays
    rig.roll.drag({from.x, laneY(10)});
    rig.refresh();
    CHECK(rig.melody()[0].velocity == 10);
    CHECK(rig.melody()[1].velocity == 1);
    // and back up: each starts from its own value at the beginning of the gesture
    rig.roll.drag({from.x, laneY(120)});
    rig.refresh();
    CHECK(rig.melody()[0].velocity == 120);
    CHECK(rig.melody()[1].velocity == 80);
    rig.roll.drag({from.x, laneY(127)});
    rig.refresh();
    CHECK(rig.melody()[1].velocity == 87);
    rig.roll.release({from.x, laneY(127)});
    REQUIRE(rig.solo.processor.undo()); // the whole drag is one step
    CHECK(rig.melody()[0].velocity == 100);
    CHECK(rig.melody()[1].velocity == 60);
}

TEST_CASE("an Alt stroke across the empty lane sets every bar it crosses", "[roll][lane][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    juce::Point<double> from{rig.xOf(500, 0.0), laneY(20)}; // no bar here
    rig.roll.pressLane(from, juce::ModifierKeys::altModifier);
    CHECK(rig.roll.gestureActive());
    const juce::Point<double> end{rig.xOf(4000, 0.0), laneY(120)};
    for (const double share : {0.4, 0.7, 1.0}) {
        rig.roll.drag({from.x + (end.x - from.x) * share, from.y + (end.y - from.y) * share});
        rig.refresh();
    }
    rig.roll.release(end);
    const auto notes = rig.melody();
    CHECK(notes[0].velocity == before[0].velocity); // the stroke began after it
    CHECK(notes[1].velocity > 20);
    CHECK(notes[1].velocity < notes[2].velocity);
    CHECK(notes[2].velocity < notes[3].velocity);
    CHECK(notes[3].velocity < 120);
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());

    // without Alt an empty lane does nothing
    rig.refresh();
    rig.roll.pressLane(from, {});
    CHECK_FALSE(rig.roll.gestureActive());
    rig.roll.drag(end);
    CHECK(rig.melody() == before);
}

TEST_CASE("a velocity drag is cancelled when somebody else changes the velocity", "[roll][lane][editor]") {
    RollRig rig;
    const juce::Point<double> from{rig.xOf(960, 2.0), laneY(100)};
    rig.roll.pressLane(from, {});
    rig.roll.drag({from.x, laneY(60)});
    rig.refresh();
    REQUIRE(rig.melody()[1].velocity == 60);
    setMelodyVelocities(rig, {{1, 30}});
    rig.roll.drag({from.x, laneY(90)});
    CHECK_FALSE(rig.roll.gestureActive());
    CHECK(rig.melody()[1].velocity == 30); // the drag did not overwrite it
    rig.roll.release(from);
}

TEST_CASE("a with several bars covering each other the topmost is grabbed", "[roll][lane][editor]") {
    RollRig rig;
    // two notes on the same start: the later one is in front
    REQUIRE(rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) {
        return mm::core::addNote(p, 1, 62, 960, 240, 90) != 0 ? size_t{1} : size_t{0};
    }));
    rig.refresh();
    const uint32_t front = rig.roll.source()[2].id; // sorted by start, stable: after the older note
    REQUIRE(rig.roll.source()[1].startTick == 960);
    rig.roll.pressLane({rig.xOf(960, 2.0), laneY(90)}, {});
    CHECK(rig.roll.selection() == std::set<uint32_t>{front});
    rig.roll.release({rig.xOf(960, 2.0), laneY(90)});
}

TEST_CASE("accent and slide of the selection toggle, mixed selections get it first", "[roll][editor]") {
    RollRig rig;
    const auto rendered = [&](size_t index) { return rig.solo.processor.playingVoices()[1].notes.at(index); };
    REQUIRE(rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) {
        const std::vector<uint32_t> ids{rig.idAt(0)};
        return mm::core::setAccent(p, 1, ids, true);
    }));
    rig.refresh();
    const auto baseVelocity = rendered(1).velocity;
    CHECK(rendered(0).velocity > baseVelocity); // the accent plays louder

    rig.roll.selectAll();
    rig.roll.toggleAccent();
    for (const auto& note : rig.melody()) {
        CHECK(note.accent); // one of four had it: now all have it
    }
    CHECK(rendered(1).velocity > baseVelocity);
    rig.refresh();
    rig.roll.toggleAccent();
    for (const auto& note : rig.melody()) {
        CHECK_FALSE(note.accent); // all had it: now none
    }
    CHECK(rendered(0).velocity == baseVelocity);
    REQUIRE(rig.solo.processor.undo()); // one step each
    for (const auto& note : rig.melody()) {
        CHECK(note.accent);
    }

    rig.refresh();
    const auto lengthBefore = rendered(0).lengthTicks;
    rig.roll.toggleSlide();
    for (const auto& note : rig.melody()) {
        CHECK(note.slide);
    }
    CHECK(rendered(0).lengthTicks > lengthBefore); // the slide reaches over the next note
    rig.refresh();
    rig.roll.toggleSlide();
    for (const auto& note : rig.melody()) {
        CHECK_FALSE(note.slide);
    }
    CHECK(rendered(0).lengthTicks == lengthBefore);
}

TEST_CASE("accent, slide, velocity and delete do nothing without a selection", "[roll][editor]") {
    RollRig rig;
    const auto before = rig.melody();
    rig.roll.toggleAccent();
    rig.roll.toggleSlide();
    rig.roll.changeVelocity(10);
    rig.roll.deleteSelection();
    rig.roll.moveSelection(240, 1);
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());
}

TEST_CASE("the velocity of the selection changes by a step and stays inside 1 to 127", "[roll][editor]") {
    RollRig rig;
    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    rig.roll.changeVelocity(-1);
    CHECK(rig.melody()[1].velocity == 99);
    rig.roll.changeVelocity(1);
    CHECK(rig.melody()[1].velocity == 100);
    rig.roll.changeVelocity(-10);
    CHECK(rig.melody()[1].velocity == 90);
    rig.roll.changeVelocity(60);
    CHECK(rig.melody()[1].velocity == 127);
    rig.roll.changeVelocity(-500);
    CHECK(rig.melody()[1].velocity == 1);
    CHECK(rig.melody()[1].pitch == 64);
    // at the edge nothing changes and nothing is recorded: one undo goes back to the step before
    rig.roll.changeVelocity(-5);
    CHECK(rig.melody()[1].velocity == 1);
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody()[1].velocity == 127);
}

TEST_CASE("the context menu shows the state of the selection and runs its items", "[roll][editor]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    const auto items = [&] {
        std::vector<std::tuple<int, juce::String, bool, bool>> found;
        juce::PopupMenu menu = rig.roll.contextMenu();
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
            if (it.getItem().itemID != 0) {
                found.emplace_back(it.getItem().itemID, it.getItem().text, it.getItem().isEnabled,
                                   it.getItem().isTicked);
            }
        }
        return found;
    };
    auto shown = items();
    REQUIRE(shown.size() == 3);
    CHECK(std::get<1>(shown[0]) == "Akzent");
    CHECK(std::get<1>(shown[1]) == "Slide");
    CHECK(std::get<1>(shown[2]) == "Löschen");
    for (const auto& item : shown) {
        CHECK_FALSE(std::get<2>(item)); // nothing selected: nothing to do
    }

    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    shown = items();
    for (const auto& item : shown) {
        CHECK(std::get<2>(item));
        CHECK_FALSE(std::get<3>(item));
    }
    rig.roll.runMenuAction(Roll::kMenuAccent);
    CHECK(rig.melody()[1].accent);
    rig.refresh();
    CHECK(std::get<3>(items()[0])); // ticked now
    CHECK_FALSE(std::get<3>(items()[1]));
    rig.roll.runMenuAction(Roll::kMenuSlide);
    CHECK(rig.melody()[1].slide);
    rig.refresh();
    CHECK(std::get<3>(items()[1]));
    rig.roll.runMenuAction(Roll::kMenuDelete);
    CHECK(rig.melody().size() == 3);
    rig.roll.runMenuAction(99); // an unknown item does nothing
    CHECK(rig.melody().size() == 3);
}

TEST_CASE("the ratchet submenu sets the repeats of the selection and shows them", "[roll][editor][ratchet]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    const auto ratchetItems = [&] {
        std::vector<std::tuple<int, juce::String, bool, bool>> found;
        juce::PopupMenu menu = rig.roll.contextMenu();
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
            const int id = it.getItem().itemID;
            if (id > Roll::kMenuRatchetBase && id <= Roll::kMenuRatchetBase + 4) {
                found.emplace_back(id, it.getItem().text, it.getItem().isEnabled, it.getItem().isTicked);
            }
        }
        return found;
    };
    auto shown = ratchetItems();
    REQUIRE(shown.size() == 4);
    CHECK(std::get<1>(shown[0]) == "Ratchet aus");
    CHECK(std::get<1>(shown[2]) == "Ratchet ×3");
    for (const auto& item : shown) {
        CHECK_FALSE(std::get<2>(item)); // nothing selected
    }
    rig.roll.setRatchet(3);
    CHECK(rig.melody()[1].ratchet == 1); // no selection: nothing changes
    CHECK_FALSE(rig.solo.processor.canUndo());

    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    shown = ratchetItems();
    for (const auto& item : shown) {
        CHECK(std::get<2>(item));
    }
    CHECK(std::get<3>(shown[0])); // ratchet off is ticked
    rig.roll.runMenuAction(Roll::kMenuRatchetBase + 3);
    CHECK(rig.melody()[1].ratchet == 3);
    rig.refresh();
    shown = ratchetItems();
    CHECK(std::get<3>(shown[2]));
    CHECK_FALSE(std::get<3>(shown[0]));
    rig.roll.runMenuAction(Roll::kMenuRatchetBase + 1);
    CHECK(rig.melody()[1].ratchet == 1);
    rig.roll.runMenuAction(Roll::kMenuRatchetBase + 4);
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody()[1].ratchet == 1);
    rig.roll.runMenuAction(Roll::kMenuRatchetBase + 5); // not an item
    CHECK(rig.melody()[1].ratchet == 1);
}

TEST_CASE("the lock submenu of the roll locks single dimensions of the selected notes", "[roll][editor][lock]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    const auto lockItems = [&] {
        std::vector<std::tuple<int, juce::String, bool, bool>> found;
        juce::PopupMenu menu = rig.roll.contextMenu();
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
            const int id = it.getItem().itemID;
            if (id > Roll::kMenuLockBase && id <= Roll::kMenuLockBase + 3) {
                found.emplace_back(id, it.getItem().text, it.getItem().isEnabled, it.getItem().isTicked);
            }
        }
        return found;
    };
    auto shown = lockItems();
    REQUIRE(shown.size() == 3);
    CHECK(std::get<1>(shown[0]) == "Tonhöhe");
    CHECK(std::get<1>(shown[1]) == "Rhythmus");
    CHECK(std::get<1>(shown[2]) == "Velocity");
    for (const auto& item : shown) {
        CHECK_FALSE(std::get<2>(item)); // nothing selected
    }
    rig.roll.toggleNoteLock(mm::core::LockDimension::Rhythm);
    CHECK(rig.melody()[1].lock == mm::core::LockFlags{}); // no selection: nothing changes
    CHECK_FALSE(rig.solo.processor.canUndo());

    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    rig.roll.runMenuAction(Roll::kMenuLockBase + 2);
    CHECK(rig.melody()[1].lock == mm::core::LockFlags(false, true, false));
    CHECK(rig.melody()[0].lock == mm::core::LockFlags{});
    rig.refresh();
    shown = lockItems();
    CHECK(std::get<3>(shown[1]));
    CHECK_FALSE(std::get<3>(shown[0]));
    CHECK(rig.note(1).lock.rhythm); // the roll knows it (it draws a mark)
    rig.roll.runMenuAction(Roll::kMenuLockBase + 1);
    rig.roll.runMenuAction(Roll::kMenuLockBase + 3);
    CHECK(rig.melody()[1].lock == mm::core::LockFlags(true, true, true));
    rig.refresh();
    rig.roll.runMenuAction(Roll::kMenuLockBase + 2); // all selected notes have it: takes it away
    CHECK(rig.melody()[1].lock == mm::core::LockFlags(true, false, true));
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody()[1].lock == mm::core::LockFlags(true, true, true));
    rig.roll.runMenuAction(Roll::kMenuLockBase + 4); // not an item
    CHECK(rig.melody()[1].lock == mm::core::LockFlags(true, true, true));
}

TEST_CASE("the lock button of a voice has a menu for the dimensions", "[roll][editor][lock]") {
    RollRig rig;
    mm::plugin::VoiceRow row(rig.solo.processor, 2);
    row.setBounds(0, 0, 900, 200);
    const auto views = rig.solo.processor.playingVoices();
    REQUIRE(views.size() >= 2);
    const auto items = [&] {
        std::vector<std::tuple<int, juce::String, bool, bool>> found;
        juce::PopupMenu menu = row.lockMenu();
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();) {
            if (it.getItem().itemID != 0) {
                found.emplace_back(it.getItem().itemID, it.getItem().text, it.getItem().isEnabled, it.getItem().isTicked);
            }
        }
        return found;
    };
    auto shown = items();
    REQUIRE(shown.size() == 4);
    for (const auto& item : shown) {
        CHECK_FALSE(std::get<2>(item)); // no view yet
    }
    row.setView(&views[1], "Melodie", true);
    shown = items();
    CHECK(std::get<1>(shown[0]) == "Tonhöhe");
    CHECK(std::get<1>(shown[3]) == "Alles");
    for (const auto& item : shown) {
        CHECK(std::get<2>(item));
        CHECK_FALSE(std::get<3>(item));
    }
    const auto lock = [&] { return rig.solo.processor.slotsSnapshot().slot(0)->pattern->voices[1].lock; };
    const auto show = [&] {
        const auto fresh = rig.solo.processor.playingVoices();
        row.setView(&fresh[1], "Melodie", true);
    };
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 2);
    CHECK(lock() == mm::core::LockFlags(false, true, false));
    show();
    CHECK(std::get<3>(items()[1]));
    CHECK_FALSE(std::get<3>(items()[0]));
    CHECK_FALSE(std::get<3>(items()[3])); // not everything
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 2); // the same item again takes the lock away
    CHECK(lock() == mm::core::LockFlags{});
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 3);
    CHECK(lock() == mm::core::LockFlags(false, false, true));
    show();
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 3);
    CHECK(lock() == mm::core::LockFlags{});
    show();
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 4); // all
    CHECK(lock() == mm::core::LockFlags(true, true, true));
    show();
    CHECK(std::get<3>(items()[3]));
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 4); // all again: free
    CHECK(lock() == mm::core::LockFlags{});
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 9); // not an item
    CHECK(lock() == mm::core::LockFlags{});
    show();
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 1);
    show();
    row.runLockAction(mm::plugin::VoiceRow::kLockMenuBase + 1); // takes it away again
    CHECK(lock() == mm::core::LockFlags{});
}

TEST_CASE("the fold shows the tones of the scale as rows and edits them", "[roll][editor][fold]") {
    RollRig rig; // A minor, notes at 60, 64, 67, 69
    CHECK_FALSE(rig.roll.fold());
    const auto before = rig.roll.viewport();
    CHECK_FALSE(rig.roll.geometry().fold.active());
    rig.roll.setHelpers(true, true, false);
    CHECK(rig.roll.fold());
    const auto fold = rig.roll.geometry().fold;
    REQUIRE(fold.active());
    CHECK(fold.tonesPerOctave() == 7);
    // the window counts rows now: it shows the same pitches
    CHECK(rig.roll.viewport().rows == before.rows);
    CHECK(rig.roll.viewport().lowPitch == static_cast<int>(std::ceil(fold.rowOf(before.lowPitch))));
    // notes on neighbouring scale tones are neighbouring rows: C (60) and the E (64) are two rows apart
    const auto geo = rig.roll.geometry();
    CHECK(geo.pitchToY(60) - geo.pitchToY(64) == Catch::Approx(2.0 * geo.rowHeight()));
    // a double click adds the scale tone of the row
    rig.roll.doubleClick({rig.xOf(1920), geo.pitchToY(62) + geo.rowHeight() / 2.0});
    bool added = false;
    for (const auto& n : rig.melody()) {
        added = added || (n.startTick == 1920 && n.pitch == 62);
    }
    CHECK(added);
    // a drag by one row moves by a scale tone: C (60) up one row is D (62)
    rig.refresh();
    const uint32_t id = rig.idAt(0);
    REQUIRE(rig.note(0).pitch == 60);
    rig.dragBy(rig.at(0, 60), 0.0, -rig.roll.geometry().rowHeight());
    CHECK(rig.melody()[0].id == id);
    CHECK(rig.melody()[0].pitch == 62);
    // switching it off gives the window of pitches back
    rig.roll.setHelpers(false, true, false);
    CHECK_FALSE(rig.roll.geometry().fold.active());
    CHECK(std::abs(rig.roll.viewport().lowPitch - before.lowPitch) <= 2);
}

TEST_CASE("the marker row toggles accent and slide of a note and makes the field lower", "[roll][editor][markers]") {
    RollRig rig;
    CHECK(rig.roll.markerHeight() == 0);
    const double height = rig.roll.geometry().height;
    rig.roll.setHelpers(false, true, true);
    CHECK(rig.roll.markers());
    CHECK(rig.roll.markerHeight() == mm::plugin::EditableRoll::kMarkerHeight);
    CHECK(rig.roll.geometry().height == height - mm::plugin::EditableRoll::kMarkerHeight);
    // a click in the upper half, at the start of the second note, sets its accent; again takes it away
    const double x = rig.roll.geometry().tickToX(960);
    rig.roll.pressMarker({x, 3.0});
    CHECK(rig.melody()[1].accent);
    CHECK_FALSE(rig.melody()[1].slide);
    rig.refresh();
    rig.roll.pressMarker({x + 2.0, 3.0});
    CHECK_FALSE(rig.melody()[1].accent);
    // the lower half is the slide
    rig.refresh();
    rig.roll.pressMarker({x, 12.0});
    CHECK(rig.melody()[1].slide);
    rig.refresh();
    // a click between the notes does nothing
    const bool anyBefore = rig.solo.processor.canUndo();
    rig.roll.pressMarker({rig.roll.geometry().tickToX(480), 3.0});
    CHECK(rig.solo.processor.canUndo() == anyBefore);
    CHECK_FALSE(rig.melody()[0].accent);
    CHECK_FALSE(rig.melody()[2].accent);
    // each toggle was one undo step
    REQUIRE(rig.solo.processor.undo());
    CHECK_FALSE(rig.melody()[1].slide);
    // without the row a press does nothing
    rig.roll.setHelpers(false, true, false);
    rig.refresh();
    const auto notes = rig.melody();
    rig.roll.pressMarker({x, 3.0});
    CHECK(rig.melody() == notes);
}

TEST_CASE("with the marker row the mouse still reaches the velocity lane and the marks", "[roll][editor][markers]") {
    RollRig rig;
    rig.roll.setHelpers(false, true, true);
    rig.refresh();
    const auto gutter = static_cast<float>(mm::plugin::EditableRoll::kGutter);
    const float laneTop =
        static_cast<float>(rig.roll.getHeight() - static_cast<int>(kLane));
    // the velocity lane is the lowest part, under the marker row
    const juce::Point<float> from(static_cast<float>(rig.xOf(960, 2.0)) + gutter,
                                  laneTop + static_cast<float>(laneY(100)));
    const juce::Point<float> to(from.x, laneTop + static_cast<float>(laneY(50)));
    rig.roll.mouseDown(mouseEventAt(rig.roll, from));
    CHECK(rig.roll.gestureActive());
    rig.roll.mouseDrag(mouseEventAt(rig.roll, to));
    rig.roll.mouseUp(mouseEventAt(rig.roll, to));
    CHECK(rig.melody()[1].velocity == 50);
    rig.refresh();
    // a click in the marker row (above the lane) toggles the accent of the note that starts there
    const float markerTop = laneTop - static_cast<float>(mm::plugin::EditableRoll::kMarkerHeight);
    const juce::Point<float> mark(static_cast<float>(rig.roll.geometry().tickToX(1920)) + gutter, markerTop + 3.0f);
    rig.roll.mouseDown(mouseEventAt(rig.roll, mark));
    rig.roll.mouseUp(mouseEventAt(rig.roll, mark));
    CHECK(rig.melody()[2].accent);
    CHECK_FALSE(rig.roll.gestureActive());
    CHECK(rig.melody()[2].velocity == 100); // the lane was not touched
}

TEST_CASE("the lane shows and edits the chance of the notes", "[roll][lane][editor][probability]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    CHECK(rig.roll.laneMode() == Roll::LaneMode::Velocity);
    const auto before = rig.melody();
    rig.roll.setLaneMode(Roll::LaneMode::Chance);
    CHECK(rig.roll.laneMode() == Roll::LaneMode::Chance);
    const auto laneHeight = static_cast<double>(Roll::kLaneHeight);
    // a drag in the lane sets the chance: a quarter of the height up from the bottom is 25 %
    const juce::Point<double> from{rig.xOf(960, 2.0), 1.0};
    rig.roll.pressLane(from, {});
    for (const double y : {laneHeight * 0.3, laneHeight * 0.55, laneHeight * 0.75}) {
        rig.roll.drag({from.x, y});
        rig.refresh();
    }
    CHECK(rig.roll.velocityHint() == 25);
    rig.roll.release({from.x, laneHeight * 0.75});
    rig.refresh();
    CHECK(rig.melody()[1].chance == 25);
    CHECK(rig.melody()[1].velocity == before[1].velocity); // the velocity lane was not touched
    CHECK(rig.melody()[0].chance == 100);
    CHECK(rig.note(1).chance == 25); // the roll knows it
    REQUIRE(rig.solo.processor.undo()); // one step
    CHECK(rig.melody()[1].chance == 100);
    CHECK_FALSE(rig.solo.processor.canUndo());
    // the bottom of the lane is 0 %, the top 100 %
    rig.roll.pressLane(from, {});
    rig.roll.drag({from.x, laneHeight + 30.0});
    rig.roll.release({from.x, laneHeight + 30.0});
    rig.refresh();
    CHECK(rig.melody()[1].chance == 0);
    rig.roll.pressLane({from.x, laneHeight - 1.0}, {});
    rig.roll.drag({from.x, -20.0});
    rig.roll.release({from.x, -20.0});
    rig.refresh();
    CHECK(rig.melody()[1].chance == 100);
    // back to the velocity: the same drag sets velocities
    rig.roll.setLaneMode(Roll::LaneMode::Velocity);
    rig.roll.pressLane({rig.xOf(960, 2.0), laneY(100)}, {});
    rig.roll.drag({rig.xOf(960, 2.0), laneY(40)});
    rig.roll.release({rig.xOf(960, 2.0), laneY(40)});
    rig.refresh();
    CHECK(rig.melody()[1].velocity == 40);
}

TEST_CASE("a selection moves by the same chance delta, a stroke with Alt sets the chance of the bars it crosses",
          "[roll][lane][editor][probability]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    rig.roll.setLaneMode(Roll::LaneMode::Chance);
    const auto laneHeight = static_cast<double>(Roll::kLaneHeight);
    // two notes with 100 and 60
    std::vector<mm::core::ChanceChange> start{{rig.idAt(0), 100}, {rig.idAt(1), 60}};
    REQUIRE(rig.solo.processor.editNotes(0, [&](mm::core::Pattern& p) { return mm::core::setChances(p, 1, start); }));
    rig.refresh();
    rig.roll.press(rig.at(0, 60), {});
    rig.roll.release(rig.at(0, 60));
    rig.roll.press(rig.at(960, 64), juce::ModifierKeys::shiftModifier);
    rig.roll.release(rig.at(960, 64));
    REQUIRE(rig.roll.selection().size() == 2);
    const juce::Point<double> from{rig.xOf(0, 2.0), 1.0};
    rig.roll.pressLane(from, {});
    rig.roll.drag({from.x, laneHeight * 0.2}); // 100 -> 80
    rig.refresh();
    CHECK(rig.melody()[0].chance == 80);
    CHECK(rig.melody()[1].chance == 40);
    CHECK(rig.melody()[2].chance == 100); // not selected
    rig.roll.release({from.x, laneHeight * 0.2});
    // a stroke with Alt over empty lane sets what it crosses
    rig.refresh();
    const juce::Point<double> strokeFrom{rig.xOf(1400, 0.0), laneHeight * 0.5}; // no bar here
    rig.roll.pressLane(strokeFrom, juce::ModifierKeys::altModifier);
    CHECK(rig.roll.gestureActive());
    for (const double share : {0.4, 0.7, 1.0}) {
        rig.roll.drag({strokeFrom.x + (rig.xOf(2400, 0.0) - strokeFrom.x) * share, laneHeight * 0.5});
        rig.refresh();
    }
    rig.roll.release({rig.xOf(2400, 0.0), laneHeight * 0.5});
    rig.refresh();
    CHECK(rig.melody()[2].chance == 50);
}

TEST_CASE("the label at the lane switches between velocity and chance", "[roll][lane][editor][probability]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    const float laneTop = static_cast<float>(rig.roll.getHeight() - Roll::kLaneHeight);
    const juce::Point<float> label(5.0f, laneTop + 4.0f);
    rig.roll.mouseDown(mouseEventAt(rig.roll, label));
    rig.roll.mouseUp(mouseEventAt(rig.roll, label));
    CHECK(rig.roll.laneMode() == Roll::LaneMode::Chance);
    CHECK_FALSE(rig.roll.gestureActive());
    rig.roll.mouseDown(mouseEventAt(rig.roll, label));
    rig.roll.mouseUp(mouseEventAt(rig.roll, label));
    CHECK(rig.roll.laneMode() == Roll::LaneMode::Velocity);
    // the gutter beside the notes does nothing
    const juce::Point<float> gutter(5.0f, 50.0f);
    rig.roll.mouseDown(mouseEventAt(rig.roll, gutter));
    rig.roll.mouseUp(mouseEventAt(rig.roll, gutter));
    CHECK(rig.roll.laneMode() == Roll::LaneMode::Velocity);
}

TEST_CASE("the menu sets the chance and the condition of the selected notes", "[roll][editor][probability]") {
    using Roll = mm::plugin::EditableRoll;
    RollRig rig;
    const auto items = [&](int low, int high) {
        std::vector<std::tuple<int, juce::String, bool, bool>> found;
        juce::PopupMenu menu = rig.roll.contextMenu();
        for (juce::PopupMenu::MenuItemIterator it(menu, true); it.next();) {
            const int id = it.getItem().itemID;
            if (id >= low && id < high) {
                found.emplace_back(id, it.getItem().text, it.getItem().isEnabled, it.getItem().isTicked);
            }
        }
        return found;
    };
    auto chances = items(Roll::kMenuChanceBase, Roll::kMenuChanceBase + 6);
    auto conditions = items(Roll::kMenuConditionBase, Roll::kMenuConditionBase + 9);
    REQUIRE(chances.size() == 6);
    REQUIRE(conditions.size() == 9);
    CHECK(std::get<1>(chances[0]) == "100 %");
    CHECK(std::get<1>(chances[2]) == "50 %");
    CHECK(std::get<1>(conditions[0]) == "immer");
    CHECK(std::get<1>(conditions[4]) == "2:4");
    for (const auto& item : chances) {
        CHECK_FALSE(std::get<2>(item)); // nothing selected
    }
    rig.roll.setChanceOfSelection(50);
    CHECK(rig.melody()[1].chance == 100); // without a selection nothing changes
    rig.roll.press(rig.at(960, 64), {});
    rig.roll.release(rig.at(960, 64));
    chances = items(Roll::kMenuChanceBase, Roll::kMenuChanceBase + 6);
    CHECK(std::get<3>(chances[0])); // 100 % is ticked
    rig.roll.runMenuAction(Roll::kMenuChanceBase + 2); // 50 %
    CHECK(rig.melody()[1].chance == 50);
    CHECK(rig.melody()[0].chance == 100);
    rig.refresh();
    chances = items(Roll::kMenuChanceBase, Roll::kMenuChanceBase + 6);
    CHECK(std::get<3>(chances[2]));
    CHECK_FALSE(std::get<3>(chances[0]));
    rig.roll.runMenuAction(Roll::kMenuConditionBase + 4); // 2:4
    CHECK(rig.melody()[1].condA == 2);
    CHECK(rig.melody()[1].condB == 4);
    rig.refresh();
    conditions = items(Roll::kMenuConditionBase, Roll::kMenuConditionBase + 9);
    CHECK(std::get<3>(conditions[4]));
    CHECK_FALSE(std::get<3>(conditions[0]));
    rig.roll.runMenuAction(Roll::kMenuConditionBase + 6); // 4:4
    CHECK(rig.melody()[1].condA == 4);
    rig.roll.runMenuAction(Roll::kMenuConditionBase + 0); // always
    CHECK(rig.melody()[1].condB == 1);
    rig.roll.runMenuAction(Roll::kMenuChanceBase + 5); // 0 %
    CHECK(rig.melody()[1].chance == 0);
    REQUIRE(rig.solo.processor.undo());
    CHECK(rig.melody()[1].chance == 50);
    rig.roll.runMenuAction(Roll::kMenuChanceBase + 6); // not an item
    rig.roll.runMenuAction(Roll::kMenuConditionBase + 9);
    CHECK(rig.melody()[1].chance == 50);
}

TEST_CASE("a new roll opened in a fold shows its notes", "[roll][editor][fold]") {
    Quiet quiet;
    Instance solo("Solo");
    mm::core::Pattern pattern = mm::core::makeEmptyPattern(2, "peak_time");
    for (const uint32_t pitch : {45u, 48u, 52u}) {
        REQUIRE(mm::core::addNote(pattern, 1, static_cast<uint8_t>(pitch), 0, 480, 100) != 0);
    }
    solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, std::move(pattern))); });
    const auto views = solo.processor.playingVoices();
    mm::plugin::EditableRoll roll(solo.processor, 2);
    roll.setBounds(0, 0, 800, 200);
    roll.setHelpers(true, true, false);
    roll.setSource(&views[1]);
    const auto geo = roll.geometry();
    REQUIRE(geo.fold.active());
    for (const auto& note : roll.source()) {
        const double y = geo.pitchToY(note.pitch);
        INFO("pitch " << int(note.pitch));
        CHECK(y >= 0.0);
        CHECK(y + geo.rowHeight() <= geo.height + 1.0);
    }
}

TEST_CASE("the roll gets the phrase borders and the kick steps of the pattern", "[roll][editor][guides]") {
    Quiet quiet;
    Instance solo("Solo");
    mm::core::Pattern pattern = mm::core::makeEmptyPattern(8, "peak_time");
    pattern.phrases.clear();
    mm::core::Phrase a;
    a.lengthBars = 4;
    mm::core::Phrase b;
    b.startBar = 4;
    b.lengthBars = 4;
    b.role = mm::core::PhraseRole::Variation;
    pattern.phrases = {a, b};
    solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, std::move(pattern))); });
    const auto views = solo.processor.playingVoices();
    REQUIRE(views.size() >= 2);
    CHECK(views[1].phraseStarts == std::vector<uint32_t>{4 * mm::core::kTicksPerBar});
    CHECK_FALSE(views[1].kickTicks.empty());
    CHECK(views[1].kickTicks.front() == 0); // four on the floor
    mm::plugin::EditableRoll roll(solo.processor, 2);
    roll.setBounds(0, 0, 800, 200);
    CHECK(roll.guides()); // on by default
    roll.setSource(&views[1]);
    roll.setHelpers(false, false, false);
    CHECK_FALSE(roll.guides());
}

TEST_CASE("the roll keeps its window when the key changes in a fold", "[roll][editor][fold]") {
    RollRig rig;
    rig.roll.setHelpers(true, true, false);
    const auto geoBefore = rig.roll.geometry();
    const int topPitch = geoBefore.fold.pitchOfRow(rig.roll.viewport().lowPitch + rig.roll.viewport().rows - 1);
    // the key moves to C: the same window of pitches, as rows of the other scale
    auto fresh = rig.solo.processor.playingVoices();
    fresh[1].root = 0;
    rig.roll.setSource(&fresh[1]);
    const auto geoAfter = rig.roll.geometry();
    const int topAfter = geoAfter.fold.pitchOfRow(rig.roll.viewport().lowPitch + rig.roll.viewport().rows - 1);
    CHECK(std::abs(topAfter - topPitch) <= 2);
    CHECK(geoAfter.fold.root == 0);
}

TEST_CASE("the mouse handlers reach the velocity lane below the notes", "[roll][lane][editor]") {
    RollRig rig;
    const auto gutter = static_cast<float>(mm::plugin::EditableRoll::kGutter);
    const float laneTop = static_cast<float>(rig.roll.getHeight() - static_cast<int>(kLane));
    const juce::Point<float> from(static_cast<float>(rig.xOf(960, 2.0)) + gutter,
                                  laneTop + static_cast<float>(laneY(100)));
    const juce::Point<float> to(from.x, laneTop + static_cast<float>(laneY(50)));
    rig.roll.mouseDown(mouseEventAt(rig.roll, from));
    CHECK(rig.roll.gestureActive());
    rig.roll.mouseDrag(mouseEventAt(rig.roll, to));
    CHECK(rig.roll.velocityHint() == 50);
    rig.roll.mouseUp(mouseEventAt(rig.roll, to));
    CHECK(rig.melody()[1].velocity == 50);
    CHECK_FALSE(rig.roll.gestureActive());
    rig.roll.mouseMove(mouseEventAt(rig.roll, from)); // the cursor change must not crash
    // the field above the lane still selects notes
    rig.refresh();
    const auto onNote = rig.at(0, 60);
    const auto title = static_cast<float>(mm::plugin::EditableRoll::kTitleHeight);
    const juce::Point<float> inField(static_cast<float>(onNote.x) + gutter, static_cast<float>(onNote.y) + title);
    rig.roll.mouseDown(mouseEventAt(rig.roll, inField));
    rig.roll.mouseUp(mouseEventAt(rig.roll, inField));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(0)});
}

TEST_CASE("the velocity lane does nothing in a voice instance or an empty slot", "[roll][lane][editor]") {
    RollRig rig;
    auto settings = rig.solo.processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    rig.solo.processor.setInstanceSettings(settings);
    const auto before = rig.melody();
    rig.roll.pressLane({rig.xOf(960, 2.0), laneY(100)}, {});
    rig.roll.drag({rig.xOf(960, 2.0), laneY(10)});
    rig.roll.release({rig.xOf(960, 2.0), laneY(10)});
    rig.roll.toggleAccent();
    CHECK(rig.melody() == before);
    CHECK_FALSE(rig.solo.processor.canUndo());

    settings.role = InstanceRole::Solo;
    rig.solo.processor.setInstanceSettings(settings);
    rig.solo.processor.editSlots([](mm::core::SlotBank& bank) { bank.clear(0); });
    rig.roll.setSource(nullptr);
    rig.roll.pressLane({100.0, 10.0}, juce::ModifierKeys::altModifier);
    CHECK_FALSE(rig.roll.gestureActive());
}

namespace {

struct FocusRig {
    FocusRig() : editor(solo.processor.createEditor()) {
        panel = dynamic_cast<mm::plugin::PlaceholderEditor*>(editor.get());
        REQUIRE(panel != nullptr);
        solo.processor.editSlots(
            [&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, markedTwoVoices(41, 42))); });
        roll1 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_1"));
        roll2 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_2"));
        REQUIRE((roll1 != nullptr && roll2 != nullptr));
        REQUIRE(waitFor([&] { return roll2->source().size() == 1; }));
    }
    juce::Component& row(int voice) { return *find(*editor, "row_" + juce::String(voice)); }
    juce::Button& focusButton(int voice) {
        return *dynamic_cast<juce::Button*>(find(*editor, "focus_" + juce::String(voice)));
    }
    /// The click is posted to the message queue: wait until the editor has the focus it should have.
    void click(int voice, int expectedFocus) {
        focusButton(voice).triggerClick();
        REQUIRE(waitFor([&] { return panel->focusedVoice() == expectedFocus; }));
    }

    Quiet quiet;
    Instance solo{"Solo"};
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    mm::plugin::PlaceholderEditor* panel = nullptr;
    mm::plugin::EditableRoll* roll1 = nullptr;
    mm::plugin::EditableRoll* roll2 = nullptr;
};

} // namespace

TEST_CASE("the focus switch gives one voice the whole height and folds the others into strips", "[focus][editor]") {
    FocusRig rig;
    CHECK(rig.panel->focusedVoice() == 0);
    CHECK(rig.focusButton(1).getButtonText() == "Fokus");
    const int half = rig.row(1).getHeight();
    CHECK(std::abs(half - rig.row(2).getHeight()) <= 1);
    CHECK(rig.roll1->isVisible());
    CHECK(rig.roll2->isVisible());

    rig.click(1, 1);
    CHECK(rig.panel->focusedVoice() == 1);
    CHECK(rig.focusButton(1).getToggleState());
    CHECK_FALSE(rig.focusButton(2).getToggleState());
    CHECK(rig.row(2).getHeight() == 36);
    CHECK(rig.row(1).getHeight() == 2 * half - 36);
    CHECK(rig.roll1->isVisible());
    CHECK_FALSE(rig.roll2->isVisible());
    // the strip keeps name, mute, lock, focus and the grip, all inside it and none of them empty
    for (const char* id : {"name_2", "mute_2", "lock_2", "focus_2", "drag_2"}) {
        auto* component = find(*rig.editor, id);
        REQUIRE(component != nullptr);
        INFO(id);
        CHECK(component->isVisible());
        CHECK(component->getWidth() > 0);
        CHECK(component->getHeight() > 0);
        CHECK(rig.row(2).getLocalBounds().contains(component->getBounds()));
    }
    // the rows still fill the area without a gap: the strip sits directly below the focused row
    CHECK(rig.row(2).getY() == rig.row(1).getBottom());

    rig.click(1, 0); // the same switch again: back to all
    CHECK(rig.panel->focusedVoice() == 0);
    CHECK_FALSE(rig.focusButton(1).getToggleState());
    CHECK(rig.roll2->isVisible());
    CHECK(rig.row(1).getHeight() == half);
    CHECK(rig.row(2).getHeight() == half);

    rig.click(1, 1);
    rig.click(2, 2); // another voice takes the focus
    CHECK(rig.panel->focusedVoice() == 2);
    CHECK_FALSE(rig.focusButton(1).getToggleState());
    CHECK(rig.focusButton(2).getToggleState());
    CHECK(rig.row(1).getHeight() == 36);
    CHECK_FALSE(rig.roll1->isVisible());
    CHECK(rig.roll2->isVisible());
}

TEST_CASE("a focused roll shows more pitch rows and keeps its place", "[focus][editor]") {
    FocusRig rig;
    const auto before = rig.roll1->viewport();
    rig.roll1->scrollBy(0, 3);
    const auto scrolled = rig.roll1->viewport();
    rig.click(1, 1);
    const auto focused = rig.roll1->viewport();
    CHECK(focused.rows > scrolled.rows);
    CHECK(focused.startTick == scrolled.startTick);
    CHECK(focused.spanTicks == scrolled.spanTicks);
    CHECK(focused.lowPitch + focused.rows / 2 == scrolled.lowPitch + scrolled.rows / 2); // the middle stays
    rig.click(1, 0);
    CHECK(rig.roll1->viewport().rows == before.rows);
}

TEST_CASE("folding a roll ends its gesture, selection and window survive", "[focus][editor]") {
    FocusRig rig;
    const auto g = rig.roll2->geometry();
    const juce::Point<double> onNote{g.tickToX(0.0) + 2.0, g.pitchToY(42) + g.rowHeight() / 2.0};
    rig.roll2->press(onNote, {});
    rig.roll2->release(onNote);
    REQUIRE(rig.roll2->selection().size() == 1);
    rig.roll2->zoomAt(10.0, 0.5);
    const auto viewport = rig.roll2->viewport();

    rig.roll2->press(onNote, {});
    rig.roll2->drag({onNote.x + 80.0, onNote.y});
    REQUIRE(rig.roll2->gestureActive());
    rig.click(1, 1); // the melody roll folds away under the mouse
    CHECK(rig.roll2->gestureActive() == false);
    rig.roll2->release(onNote); // a late mouse-up of the folded roll is harmless

    rig.click(1, 0);
    CHECK(rig.roll2->isVisible());
    CHECK(rig.roll2->selection().size() == 1);
    CHECK(rig.roll2->viewport().startTick == viewport.startTick);
    CHECK(rig.roll2->viewport().spanTicks == viewport.spanTicks);
}

TEST_CASE("the focus goes back to all when the focused voice is gone", "[focus][editor]") {
    FocusRig rig;
    mm::core::Pattern three = markedTwoVoices(41, 42);
    three.voices.push_back(three.voices[1]);
    three.voices.back().midiChannel = 3;
    three.voices.back().notes[0].id = mm::core::allocateNoteId(three);
    rig.solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(1, three)); });
    rig.solo.processor.selectSlot(2);
    REQUIRE(waitFor([&] { return find(*rig.editor, "focus_3") != nullptr; }));

    rig.click(3, 3);
    REQUIRE(rig.panel->focusedVoice() == 3);
    CHECK(rig.row(1).getHeight() == 36);
    CHECK(rig.row(2).getHeight() == 36);

    rig.solo.processor.selectSlot(1); // two voices again
    REQUIRE(waitFor([&] { return find(*rig.editor, "focus_3") == nullptr; }));
    CHECK(rig.panel->focusedVoice() == 0);
    CHECK(rig.row(1).getHeight() > 36);
    CHECK(std::abs(rig.row(1).getHeight() - rig.row(2).getHeight()) <= 1);
    CHECK_FALSE(rig.focusButton(1).getToggleState());
}

TEST_CASE("a focus on a voice that still exists survives another pattern", "[focus][editor]") {
    FocusRig rig;
    rig.click(2, 2);
    rig.solo.processor.editSlots(
        [&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, markedTwoVoices(43, 44))); });
    REQUIRE(waitFor([&] { return rig.roll2->source().front().pitch == 44; }));
    CHECK(rig.panel->focusedVoice() == 2);
    CHECK(rig.row(1).getHeight() == 36);
    CHECK(rig.focusButton(2).getToggleState());
}

TEST_CASE("the strip of a folded row still mutes and locks its voice", "[focus][editor]") {
    FocusRig rig;
    rig.click(1, 1);
    auto* lock = dynamic_cast<juce::Button*>(find(*rig.editor, "lock_2"));
    REQUIRE(lock != nullptr);
    REQUIRE(waitFor([&] { return lock->isEnabled(); }));
    lock->triggerClick();
    REQUIRE(waitFor([&] { return rig.solo.processor.playingVoices()[1].locked; }));
    auto* mute = dynamic_cast<juce::Button*>(find(*rig.editor, "mute_2"));
    REQUIRE(mute != nullptr);
    mute->triggerClick();
    REQUIRE(waitFor([&] { return mute->getToggleState(); }));
}

// --- Buttons for the actions and the history (P2-G2, D-161) ---------------------------------------------------------

namespace {

mm::core::Pattern generatedPattern(const mm::plugin::ProcessorBase& processor, uint64_t seed) {
    mm::core::GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    const auto result = mm::core::generatePattern(*processor.styles().findOrFallback("peak_time"), request);
    REQUIRE(result.success);
    return result.pattern;
}

/// A solo instance with its editor and a pattern in slot 1: two marked notes, or a generated pattern.
struct ActionRig {
    explicit ActionRig(bool generated = false) : editor(solo.processor.createEditor()) {
        panel = dynamic_cast<mm::plugin::PlaceholderEditor*>(editor.get());
        REQUIRE(panel != nullptr);
        const auto pattern = generated ? generatedPattern(solo.processor, 5) : markedTwoVoices(41, 42);
        solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });
        roll1 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_1"));
        roll2 = dynamic_cast<mm::plugin::EditableRoll*>(find(*editor, "roll_2"));
        REQUIRE((roll1 != nullptr && roll2 != nullptr));
        REQUIRE(waitFor([&] { return roll2->source().size() == pattern.voices[1].notes.size(); }));
    }
    juce::Button& button(const juce::String& id) {
        auto* found = dynamic_cast<juce::Button*>(find(*editor, id));
        REQUIRE(found != nullptr);
        return *found;
    }
    juce::Slider& strength() {
        auto* found = dynamic_cast<juce::Slider*>(find(*editor, "strength"));
        REQUIRE(found != nullptr);
        return *found;
    }
    juce::String historyText() {
        auto* label = dynamic_cast<juce::Label*>(find(*editor, "history_label"));
        REQUIRE(label != nullptr);
        return label->getText();
    }
    mm::core::Pattern pattern(size_t slot = 0) const { return *solo.processor.slotsSnapshot().slot(slot)->pattern; }
    size_t historySize(size_t slot = 0) const { return solo.processor.slotsSnapshot().slot(slot)->history.size(); }
    /// Clicks are posted to the message queue: wait for `done` after each.
    void click(const juce::String& id, const std::function<bool()>& done) {
        button(id).triggerClick();
        REQUIRE(waitFor(done));
    }
    bool enabled(const juce::String& id) { return button(id).isEnabled(); }

    Quiet quiet;
    Instance solo{"Solo"};
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    mm::plugin::PlaceholderEditor* panel = nullptr;
    mm::plugin::EditableRoll* roll1 = nullptr;
    mm::plugin::EditableRoll* roll2 = nullptr;
};

} // namespace

TEST_CASE("an empty slot leaves every action button disabled", "[buttons][editor]") {
    Quiet quiet;
    Instance solo("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(solo.processor.createEditor());
    for (const char* id : {"renew_1", "vary_1", "duplicate_1", "renew_2", "vary_2", "vary_all", "undo", "redo",
                           "history_back", "history_forward"}) {
        auto* button = dynamic_cast<juce::Button*>(find(*editor, id));
        REQUIRE(button != nullptr);
        INFO(id);
        CHECK_FALSE(button->isEnabled());
    }
    auto* label = dynamic_cast<juce::Label*>(find(*editor, "history_label"));
    REQUIRE(label != nullptr);
    CHECK(label->getText() == "Verlauf –");
}

TEST_CASE("the action buttons carry their texts and the strength starts at 30 percent", "[buttons][editor]") {
    ActionRig rig(true);
    CHECK(rig.button("renew_1").getButtonText() == "Neu");
    CHECK(rig.button("vary_1").getButtonText() == "Variation");
    CHECK(rig.button("duplicate_1").getButtonText() == "Duplizieren");
    CHECK(rig.button("vary_all").getButtonText() == "Variation (alle)");
    CHECK(rig.button("undo").getButtonText() == "Rückgängig");
    CHECK(rig.button("redo").getButtonText() == "Wiederholen");
    CHECK(rig.panel->variationStrength() == 30);
    CHECK(rig.strength().getMinimum() == 0.0);
    CHECK(rig.strength().getMaximum() == 100.0);
    REQUIRE(waitFor([&] { return rig.enabled("renew_1"); }));
    CHECK(rig.enabled("vary_1"));
    CHECK(rig.enabled("vary_all"));
    CHECK_FALSE(rig.enabled("duplicate_1")); // nothing selected
    CHECK_FALSE(rig.enabled("undo"));
    CHECK_FALSE(rig.enabled("redo"));
    CHECK_FALSE(rig.enabled("history_back"));
    CHECK_FALSE(rig.enabled("history_forward"));
    CHECK(rig.historyText() == "Verlauf 1/1");
}

TEST_CASE("Neu renews that voice only, as one history entry", "[buttons][editor]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("renew_2"); }));
    const auto before = rig.pattern();
    rig.click("renew_2", [&] { return rig.historySize() == 2; });
    const auto after = rig.pattern();
    CHECK(after.voices[0].notes == before.voices[0].notes);
    CHECK(after.voices[1].notes != before.voices[1].notes);
    CHECK(rig.solo.processor.generationStatus() == mm::plugin::GenerationStatus::Renewed);
    REQUIRE(waitFor([&] { return rig.enabled("undo"); }));
    CHECK(rig.historyText() == "Verlauf 2/2");
    // the first voice has its own button
    rig.click("renew_1", [&] { return rig.historySize() == 3; });
    CHECK(rig.pattern().voices[1].notes == after.voices[1].notes);
    CHECK(rig.pattern().voices[0].notes != before.voices[0].notes);
}

TEST_CASE("a locked voice has neither Neu nor Variation", "[buttons][editor][lock]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("lock_1") && rig.enabled("renew_1"); }));
    rig.click("lock_1", [&] { return rig.solo.processor.playingVoices()[0].locked; });
    REQUIRE(waitFor([&] { return !rig.enabled("renew_1"); }));
    CHECK_FALSE(rig.enabled("vary_1"));
    CHECK(rig.enabled("renew_2"));
    CHECK(rig.enabled("vary_2"));
    CHECK(rig.enabled("vary_all")); // one voice is still free
    rig.click("lock_2", [&] { return rig.solo.processor.playingVoices()[1].locked; });
    REQUIRE(waitFor([&] { return !rig.enabled("vary_all") || !rig.enabled("renew_2"); }));
    rig.click("lock_1", [&] { return !rig.solo.processor.playingVoices()[0].locked; }); // unlock again
    REQUIRE(waitFor([&] { return rig.enabled("renew_1"); }));
    CHECK(rig.enabled("vary_1"));
}

TEST_CASE("Variation uses the strength of the slider and varies that voice only", "[buttons][editor]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("vary_1"); }));
    const auto before = rig.pattern();
    rig.strength().setValue(0.0, juce::sendNotificationSync);
    rig.button("vary_2").triggerClick();
    REQUIRE(
        waitFor([&] { return rig.solo.processor.generationStatus() == mm::plugin::GenerationStatus::NothingToVary; }));
    CHECK(rig.historySize() == 1); // strength 0 changes nothing
    rig.strength().setValue(70.0, juce::sendNotificationSync);
    CHECK(rig.panel->variationStrength() == 70);
    rig.click("vary_2", [&] { return rig.historySize() == 2; });
    CHECK(rig.solo.processor.generationStatus() == mm::plugin::GenerationStatus::Varied);
    CHECK(rig.pattern().voices[0].notes == before.voices[0].notes);
    CHECK(rig.pattern().voices[1].notes != before.voices[1].notes);
    CHECK(rig.pattern().info.source == "variation");
}

TEST_CASE("Variation (alle) varies the free voices and spares the locked one", "[buttons][editor][lock]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("lock_1") && rig.enabled("vary_all"); }));
    rig.click("lock_1", [&] { return rig.solo.processor.playingVoices()[0].locked; });
    const auto before = rig.pattern();
    rig.strength().setValue(100.0, juce::sendNotificationSync);
    const auto steps = rig.historySize();
    rig.click("vary_all", [&] { return rig.historySize() == steps + 1; });
    CHECK(rig.pattern().voices[0].notes == before.voices[0].notes);
    CHECK(rig.pattern().voices[1].notes != before.voices[1].notes);
}

TEST_CASE("Variation (alle) without a lock varies the whole pattern", "[buttons][editor]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("vary_all"); }));
    const auto before = rig.pattern();
    rig.strength().setValue(100.0, juce::sendNotificationSync);
    rig.click("vary_all", [&] { return rig.historySize() == 2; });
    CHECK(rig.pattern().voices != before.voices);
}

TEST_CASE("Rückgängig and Wiederholen step through the undo stack", "[buttons][editor]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("renew_2"); }));
    const auto before = rig.pattern();
    rig.click("renew_2", [&] { return rig.historySize() == 2; });
    const auto renewed = rig.pattern();
    REQUIRE(waitFor([&] { return rig.enabled("undo"); }));
    CHECK_FALSE(rig.enabled("redo"));
    rig.click("undo", [&] { return rig.historySize() == 1; });
    CHECK(rig.pattern().voices == before.voices);
    REQUIRE(waitFor([&] { return rig.enabled("redo") && !rig.enabled("undo"); }));
    rig.click("redo", [&] { return rig.historySize() == 2; });
    CHECK(rig.pattern().voices == renewed.voices);
    REQUIRE(waitFor([&] { return rig.enabled("undo") && !rig.enabled("redo"); }));
}

TEST_CASE("the history buttons browse the results and show the position", "[buttons][editor][history]") {
    ActionRig rig(true);
    REQUIRE(waitFor([&] { return rig.enabled("renew_2"); }));
    rig.click("renew_2", [&] { return rig.historySize() == 2; });
    rig.strength().setValue(100.0, juce::sendNotificationSync);
    rig.click("vary_all", [&] { return rig.historySize() == 3; });
    const auto history = rig.solo.processor.slotsSnapshot().slot(0)->history;
    REQUIRE(waitFor([&] { return rig.historyText() == "Verlauf 3/3"; }));
    CHECK(rig.enabled("history_back"));
    CHECK_FALSE(rig.enabled("history_forward"));
    CHECK(rig.enabled("undo"));

    rig.click("history_back", [&] { return rig.solo.processor.slotsSnapshot().slot(0)->cursor == 1; });
    CHECK(rig.pattern() == history[1]);
    CHECK(rig.historyText() == "Verlauf 2/3");
    CHECK(rig.enabled("history_forward"));
    CHECK_FALSE(rig.enabled("undo")); // browsing drops the undo steps of the slot
    rig.click("history_back", [&] { return rig.solo.processor.slotsSnapshot().slot(0)->cursor == 0; });
    CHECK(rig.pattern() == history[0]);
    CHECK(rig.historyText() == "Verlauf 1/3");
    CHECK_FALSE(rig.enabled("history_back"));
    rig.click("history_forward", [&] { return rig.solo.processor.slotsSnapshot().slot(0)->cursor == 1; });
    rig.click("history_forward", [&] { return rig.solo.processor.slotsSnapshot().slot(0)->cursor == 2; });
    CHECK(rig.pattern() == history[2]);
    CHECK(rig.historyText() == "Verlauf 3/3");
    CHECK_FALSE(rig.enabled("history_forward"));
    CHECK(rig.historySize() == 3);
}

TEST_CASE("Duplizieren copies the selection of the roll and selects the copy", "[buttons][editor][duplicate]") {
    ActionRig rig;
    CHECK_FALSE(rig.enabled("duplicate_1"));
    rig.roll1->selectAll();
    REQUIRE(waitFor([&] { return rig.enabled("duplicate_1"); }));
    CHECK_FALSE(rig.enabled("duplicate_2")); // the other roll has no selection
    rig.click("duplicate_1", [&] { return rig.pattern().voices[0].notes.size() == 2; });
    const auto notes = rig.pattern().voices[0].notes;
    CHECK(notes[1].startTick == notes[0].startTick + 240); // one grid step (1/16) behind the note
    CHECK(notes[1].pitch == notes[0].pitch);
    CHECK(rig.pattern().info.source == "edit");
    CHECK(rig.pattern().voices[1].notes.size() == 1);
    REQUIRE(waitFor([&] { return rig.roll1->source().size() == 2 && rig.roll1->selection().size() == 1; }));
    CHECK(*rig.roll1->selection().begin() == notes[1].id);
    // the copy is selected: another press goes on behind it
    rig.click("duplicate_1", [&] { return rig.pattern().voices[0].notes.size() == 3; });
    CHECK(rig.pattern().voices[0].notes[2].startTick == 480);
    // one undo step each
    REQUIRE(waitFor([&] { return rig.enabled("undo"); }));
    rig.click("undo", [&] { return rig.pattern().voices[0].notes.size() == 2; });
    rig.click("undo", [&] { return rig.pattern().voices[0].notes.size() == 1; });
}

TEST_CASE("Duplizieren rounds the offset up to the grid of the roll", "[buttons][editor][duplicate]") {
    ActionRig rig;
    rig.roll1->setEditSettings(mm::core::EditGrid{4, false}, mm::core::PitchSnap::Scale); // 1/4: 960 ticks
    rig.roll1->selectAll();
    REQUIRE(waitFor([&] { return rig.enabled("duplicate_1"); }));
    rig.click("duplicate_1", [&] { return rig.pattern().voices[0].notes.size() == 2; });
    CHECK(rig.pattern().voices[0].notes[1].startTick == 960);
}

TEST_CASE("the strip of a folded row keeps its action buttons", "[buttons][editor][focus]") {
    ActionRig rig;
    rig.button("focus_1").triggerClick();
    REQUIRE(waitFor([&] { return rig.panel->focusedVoice() == 1; }));
    auto& row = *find(*rig.editor, "row_2");
    REQUIRE(row.getHeight() == 36);
    for (const char* id : {"renew_2", "vary_2", "duplicate_2", "mute_2", "lock_2", "focus_2", "drag_2"}) {
        auto* component = find(*rig.editor, id);
        REQUIRE(component != nullptr);
        INFO(id);
        CHECK(component->isVisible());
        CHECK(component->getWidth() > 0);
        CHECK(component->getHeight() > 0);
        CHECK(row.getLocalBounds().contains(component->getBounds()));
    }
    // the buttons of the strip work
    REQUIRE(waitFor([&] { return rig.enabled("renew_2"); }));
    rig.click("renew_2", [&] { return rig.historySize() == 2; });
}

TEST_CASE("the buttons of a row act on the slot the row shows", "[buttons][editor]") {
    ActionRig rig;
    rig.solo.processor.editSlots(
        [&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(2, generatedPattern(rig.solo.processor, 6))); });
    rig.solo.processor.selectSlot(3);
    REQUIRE(waitFor([&] { return rig.roll1->source().size() > 1; })); // the generated pattern of slot 3
    REQUIRE(waitFor([&] { return rig.enabled("renew_1"); }));
    rig.click("renew_1", [&] { return rig.historySize(2) == 2; });
    CHECK(rig.historySize(0) == 1);
    rig.strength().setValue(80.0, juce::sendNotificationSync);
    rig.click("vary_2", [&] { return rig.historySize(2) == 3; });
    CHECK(rig.historySize(0) == 1);
    REQUIRE(waitFor([&] { return rig.historyText() == "Verlauf 3/3"; }));
    rig.click("history_back", [&] { return rig.solo.processor.slotsSnapshot().slot(2)->cursor == 1; });
    CHECK(rig.solo.processor.slotsSnapshot().slot(0)->cursor == 0);
}

TEST_CASE("until the bar line the rows act on the slot that plays, not on the one just selected", "[buttons][editor]") {
    ActionRig rig(true);
    rig.solo.processor.editSlots(
        [&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(2, generatedPattern(rig.solo.processor, 6))); });
    runTo({&rig.solo}, 1.0);
    rig.solo.processor.selectSlot(3); // takes effect at PPQ 4
    CHECK(rig.solo.processor.playingSlotInfo().slot == 1);
    REQUIRE(waitFor([&] { return rig.enabled("renew_1"); }));
    rig.click("renew_1", [&] { return rig.historySize(0) == 2; });
    rig.strength().setValue(80.0, juce::sendNotificationSync);
    rig.click("vary_2", [&] { return rig.historySize(0) == 3; });
    CHECK(rig.historySize(2) == 1);
    REQUIRE(waitFor([&] { return rig.historyText() == "Verlauf 3/3"; }));
    rig.click("history_back", [&] { return rig.solo.processor.slotsSnapshot().slot(0)->cursor == 1; });
    CHECK(rig.historySize(2) == 1);
}

TEST_CASE("a voice instance has no action buttons", "[buttons][editor]") {
    Quiet quiet;
    Instance voice("Voice");
    auto settings = voice.processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    voice.processor.setInstanceSettings(settings);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    REQUIRE(waitFor([&] { return editor->getHeight() == 320; }));
    for (const char* id :
         {"vary_all", "undo", "redo", "history_back", "history_forward", "history_label", "strength"}) {
        auto* component = find(*editor, id);
        REQUIRE(component != nullptr);
        INFO(id);
        CHECK_FALSE(component->isVisible());
    }
    for (const char* id : {"row_1", "row_2"}) {
        if (auto* row = find(*editor, id)) {
            CHECK_FALSE(row->isVisible());
        }
    }
}

// --- Levels and the user's settings file (P2-H, D-162) --------------------------------------------------------------

namespace {

/// A settings store on a file in a fresh temporary folder, installed as the store of the process while it lives. It
/// must outlive every instance and editor that is created while it is installed.
struct TempSettings {
    TempSettings()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory)
                     .getChildFile("mm_settings_" +
                                   juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()))),
          file(folder.getChildFile("sub").getChildFile("settings.json")) {}
    ~TempSettings() {
        mm::plugin::overrideGlobalSettings(&mmtest::disabledSettings());
        store.reset();
        folder.deleteRecursively();
    }
    /// Starts reading the file (it may not exist) and installs the store.
    mm::plugin::GlobalSettingsStore& open(bool readNow = true) {
        store = std::make_unique<mm::plugin::GlobalSettingsStore>(file, readNow);
        mm::plugin::overrideGlobalSettings(store.get());
        return *store;
    }
    void writeFile(const juce::String& text) {
        REQUIRE(file.getParentDirectory().createDirectory());
        REQUIRE(file.replaceWithText(text));
    }
    juce::File folder;
    juce::File file;
    std::unique_ptr<mm::plugin::GlobalSettingsStore> store;
};

} // namespace

TEST_CASE("the settings store reads a missing file as the defaults and creates the file on the first change",
          "[settings][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    CHECK(store.get() == mm::core::GlobalSettings{});
    CHECK_FALSE(temp.file.existsAsFile());
    store.update([](mm::core::GlobalSettings& settings) {
        settings.expert = true;
        settings.startRoot = 2;
        settings.variationStrength = 61;
    });
    store.waitForWrites();
    REQUIRE(temp.file.existsAsFile()); // the folder was created, too
    CHECK_FALSE(temp.file.getSiblingFile("settings.json.tmp").exists());
    const auto written = mm::core::parseGlobalSettings(temp.file.loadFileAsString().toStdString());
    CHECK(written.expert);
    CHECK(written.startRoot == 2);
    CHECK(written.variationStrength == 61);
    CHECK(written == store.get());
}

TEST_CASE("another store on the same file reads what was written", "[settings][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    {
        auto& first = temp.open();
        REQUIRE(waitFor([&] { return first.ready(); }));
        first.update([](mm::core::GlobalSettings& settings) {
            settings.startRoot = 7;
            settings.startScaleId = "dorian";
            settings.grid = mm::core::EditGrid{8, true};
            settings.snapChromatic = true;
            settings.rollFold = true;
            settings.rollGuides = false;
            settings.rollMarkers = true;
            settings.shortcuts = {{"generate", "cmd+g"}};
            settings.middleC = 4;
            settings.language = "en";
        });
        first.waitForWrites();
    }
    auto& second = temp.open();
    REQUIRE(waitFor([&] { return second.ready(); }));
    CHECK(second.get().startRoot == 7);
    CHECK(second.get().startScaleId == "dorian");
    CHECK(second.get().grid == mm::core::EditGrid{8, true});
    CHECK(second.get().snapChromatic);
    CHECK(second.get().rollFold);
    CHECK_FALSE(second.get().rollGuides);
    CHECK(second.get().rollMarkers);
    CHECK(second.get().shortcuts == std::map<std::string, std::string>{{"generate", "cmd+g"}});
    CHECK(second.get().middleC == 4);
    CHECK(second.get().language == "en");
}

TEST_CASE("a change before the file is read wins for its field and keeps the rest of the file", "[settings][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    const juce::String original = R"({"version": 1, "startRoot": 2, "startScale": "dorian", "variationStrength": 55})";
    temp.writeFile(original);
    auto& store = temp.open(false);
    store.update([](mm::core::GlobalSettings& settings) {
        settings.expert = true;
        settings.startRoot = 7;
        settings.grid = mm::core::EditGrid{8, true};
        settings.snapChromatic = true;
        settings.rollFold = true;
        settings.rollGuides = false;
        settings.rollMarkers = true;
        settings.shortcuts = {{"generate", "cmd+g"}};
        settings.middleC = 4;
        settings.language = "en";
    });
    CHECK(store.get().expert); // visible at once
    store.waitForWrites();
    CHECK(temp.file.loadFileAsString() == original); // nothing is written before the file was read
    store.beginRead();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.waitForWrites();
    const auto now = store.get();
    CHECK(now.expert);
    CHECK(now.startRoot == 7);
    CHECK(now.grid == mm::core::EditGrid{8, true});
    CHECK(now.snapChromatic);
    CHECK(now.rollFold);
    CHECK_FALSE(now.rollGuides);
    CHECK(now.rollMarkers);
    CHECK(now.shortcuts == std::map<std::string, std::string>{{"generate", "cmd+g"}});
    CHECK(now.middleC == 4);
    CHECK(now.language == "en");
    CHECK(now.startScaleId == "dorian");
    CHECK(now.variationStrength == 55);
    CHECK(mm::core::parseGlobalSettings(temp.file.loadFileAsString().toStdString()) == now); // and the file says so
}

TEST_CASE("a bad settings file is read as the defaults", "[settings][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    temp.writeFile("{ this is not json");
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    CHECK(store.get() == mm::core::GlobalSettings{});
}

TEST_CASE("the settings store sanitizes what it is given", "[settings][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) {
        settings.variationStrength = 500;
        settings.startScaleId = "nope";
    });
    CHECK(store.get().variationStrength == 100);
    CHECK(store.get().startScaleId == "natural_minor");
}

TEST_CASE("a store without file reads and writes nothing", "[settings][store]") {
    mm::plugin::GlobalSettingsStore store{juce::File()};
    CHECK_FALSE(store.enabled());
    CHECK(store.ready());
    store.update([](mm::core::GlobalSettings& settings) { settings.expert = true; });
    CHECK_FALSE(store.get().expert);
    store.waitForWrites();
}

TEST_CASE("the default settings file lies in the user's data folder under Klirrwerk", "[settings][store]") {
    const auto file = mm::plugin::GlobalSettingsStore::defaultFile();
    if (juce::SystemStats::getEnvironmentVariable("MIDIMAID_SETTINGS_FILE", {}).isEmpty()) {
        CHECK(file.getFileName() == "settings.json");
        CHECK(file.getParentDirectory().getFileName() == "MidiMaid");
        CHECK(file.getParentDirectory().getParentDirectory().getFileName() == "Klirrwerk");
        if ((juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX) != 0) {
            CHECK(file.getFullPathName().contains("/Library/Application Support/Klirrwerk/MidiMaid/"));
        }
    }
}

TEST_CASE("a new instance starts with the key the user chose last", "[settings][startkey]") {
    TempSettings temp;
    temp.writeFile(R"({"version": 1, "startRoot": 2, "startScale": "dorian"})");
    auto& store = temp.open(false);
    Quiet quiet;
    Instance instance("Solo"); // created before the file is read: the key arrives when it is there
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
    CHECK(instance.processor.instanceSettings().generation.root == 9);
    store.beginRead();
    REQUIRE(waitFor([&] { return instance.processor.instanceSettings().generation.root == 2; }));
    CHECK(instance.processor.instanceSettings().generation.scaleId == "dorian");
    CHECK(store.ready());
}

TEST_CASE("the very first start keeps A minor", "[settings][startkey]") {
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Quiet quiet;
    Instance instance("Solo");
    const auto generation = instance.processor.instanceSettings().generation;
    CHECK(generation.root == 9);
    CHECK(generation.scaleId == "natural_minor");
}

TEST_CASE("a project keeps its own key, whatever the user chose last", "[settings][startkey]") {
    // also a project that saved the built-in A minor: it must not turn into the key chosen last
    const std::vector<std::pair<int, std::string>> keys = {{5, "phrygian"}, {9, "natural_minor"}};
    for (const auto& [root, scale] : keys) {
        INFO(root << " " << scale);
        juce::MemoryBlock saved;
        {
            Quiet quiet;
            Instance first("Solo");
            auto settings = first.processor.instanceSettings();
            settings.generation.root = static_cast<mm::core::PitchClass>(root);
            settings.generation.scaleId = scale;
            first.processor.setInstanceSettings(settings);
            first.processor.getStateInformation(saved);
        }
        TempSettings temp;
        temp.writeFile(R"({"version": 1, "startRoot": 2, "startScale": "dorian"})");
        auto& store = temp.open(false);
        Quiet quiet;
        Instance loaded("Solo"); // created before the file is read
        loaded.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        store.beginRead();
        REQUIRE(waitFor([&] { return store.ready(); }));
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        CHECK(loaded.processor.instanceSettings().generation.root == root);
        CHECK(loaded.processor.instanceSettings().generation.scaleId == scale);
    }
}

TEST_CASE("a key chosen before the file is read stays", "[settings][startkey]") {
    TempSettings temp;
    temp.writeFile(R"({"version": 1, "startRoot": 2, "startScale": "dorian"})");
    auto& store = temp.open(false);
    Quiet quiet;
    Instance instance("Solo");
    auto settings = instance.processor.instanceSettings();
    settings.generation.root = 5;
    settings.generation.scaleId = "phrygian";
    instance.processor.setInstanceSettings(settings);
    store.beginRead();
    REQUIRE(waitFor([&] { return store.ready(); }));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    CHECK(instance.processor.instanceSettings().generation.root == 5);
    CHECK(instance.processor.instanceSettings().generation.scaleId == "phrygian");
}

TEST_CASE("a key chosen in the window becomes the start key of the next instance", "[settings][startkey][editor]") {
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Quiet quiet;
    Instance instance("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
    auto* keys = dynamic_cast<juce::ComboBox*>(find(*editor, "key"));
    auto* scales = dynamic_cast<juce::ComboBox*>(find(*editor, "scale"));
    REQUIRE((keys != nullptr && scales != nullptr));
    keys->setSelectedId(2 + 4, juce::sendNotificationSync); // E
    scales->setSelectedId(3, juce::sendNotificationSync);   // the second scale of the list
    const auto chosen = store.get();
    CHECK(chosen.startRoot == 4);
    CHECK(chosen.startScaleId == instance.processor.instanceSettings().generation.scaleId);
    keys->setSelectedId(1, juce::sendNotificationSync); // "auto" is no key to remember
    scales->setSelectedId(1, juce::sendNotificationSync);
    CHECK(store.get().startRoot == 4);
    CHECK(store.get().startScaleId == chosen.startScaleId);
    store.waitForWrites();
    editor.reset();
    Instance next("Solo");
    CHECK(next.processor.instanceSettings().generation.root == 4);
    CHECK(next.processor.instanceSettings().generation.scaleId == chosen.startScaleId);
}

TEST_CASE("the basic level hides the expert controls, the switch shows them and the file remembers it",
          "[settings][level][editor]") {
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Quiet quiet;
    Instance instance("Solo");
    const std::vector<const char*> expertIds = {"seed",     "random",   "grid_box", "triplet",
                                                "snap_box", "vary_all", "strength"};
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        auto* panel = dynamic_cast<mm::plugin::PlaceholderEditor*>(editor.get());
        REQUIRE(panel != nullptr);
        auto* toggle = dynamic_cast<juce::Button*>(find(*editor, "expert"));
        REQUIRE(toggle != nullptr);
        CHECK(toggle->getButtonText() == "Experte");
        CHECK(toggle->isVisible());
        CHECK_FALSE(panel->expertLevel());
        CHECK_FALSE(toggle->getToggleState());
        for (const char* id : expertIds) {
            INFO(id);
            REQUIRE(find(*editor, id) != nullptr);
            CHECK_FALSE(find(*editor, id)->isVisible());
        }
        // what belongs to the basic level stays
        for (const char* id : {"style", "key", "scale", "bars", "energy", "creativity", "undo", "redo", "slot1"}) {
            INFO(id);
            REQUIRE(find(*editor, id) != nullptr);
            CHECK(find(*editor, id)->isVisible());
        }
        CHECK(find(*editor, "renew_1")->isVisible()); // the buttons of the rows are basic

        toggle->triggerClick();
        REQUIRE(waitFor([&] { return panel->expertLevel(); }));
        for (const char* id : expertIds) {
            INFO(id);
            CHECK(find(*editor, id)->isVisible());
            CHECK(find(*editor, id)->getWidth() > 0);
        }
        CHECK(store.get().expert);
        store.waitForWrites();
        CHECK(mm::core::parseGlobalSettings(temp.file.loadFileAsString().toStdString()).expert);
    }
    // a new window opens at the level the user left
    std::unique_ptr<juce::AudioProcessorEditor> again(instance.processor.createEditor());
    auto* panel = dynamic_cast<mm::plugin::PlaceholderEditor*>(again.get());
    CHECK(panel->expertLevel());
    CHECK(find(*again, "seed")->isVisible());
    CHECK(dynamic_cast<juce::Button*>(find(*again, "expert"))->getToggleState());
    dynamic_cast<juce::Button*>(find(*again, "expert"))->triggerClick();
    REQUIRE(waitFor([&] { return !panel->expertLevel(); }));
    CHECK_FALSE(find(*again, "seed")->isVisible());
    CHECK_FALSE(store.get().expert);
}

TEST_CASE("strength, grid and snapping are remembered across windows", "[settings][level][editor]") {
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Quiet quiet;
    Instance instance("Solo");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        auto* strength = dynamic_cast<juce::Slider*>(find(*editor, "strength"));
        auto* grid = dynamic_cast<juce::ComboBox*>(find(*editor, "grid_box"));
        auto* triplet = dynamic_cast<juce::Button*>(find(*editor, "triplet"));
        auto* snap = dynamic_cast<juce::ComboBox*>(find(*editor, "snap_box"));
        REQUIRE((strength != nullptr && grid != nullptr && triplet != nullptr && snap != nullptr));
        strength->setValue(64.0, juce::sendNotificationSync);
        grid->setSelectedId(8, juce::sendNotificationSync);
        triplet->setToggleState(true, juce::sendNotificationSync);
        snap->setSelectedId(2, juce::sendNotificationSync);
        const auto saved = store.get();
        CHECK(saved.variationStrength == 64);
        CHECK(saved.grid == mm::core::EditGrid{8, true});
        CHECK(saved.snapChromatic);
    }
    store.waitForWrites();
    std::unique_ptr<juce::AudioProcessorEditor> again(instance.processor.createEditor());
    auto* panel = dynamic_cast<mm::plugin::PlaceholderEditor*>(again.get());
    CHECK(panel->variationStrength() == 64);
    CHECK(dynamic_cast<juce::ComboBox*>(find(*again, "grid_box"))->getSelectedId() == 8);
    CHECK(dynamic_cast<juce::Button*>(find(*again, "triplet"))->getToggleState());
    CHECK(dynamic_cast<juce::ComboBox*>(find(*again, "snap_box"))->getSelectedId() == 2);
    // the roll works with the remembered grid: 1/8 triplet = 2/3 of 480 ticks
    instance.processor.editSlots(
        [&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, markedTwoVoices(41, 42))); });
    auto* roll = dynamic_cast<mm::plugin::EditableRoll*>(find(*again, "roll_1"));
    REQUIRE(roll != nullptr);
    CHECK(roll->snap().grid == mm::core::EditGrid{8, true});
}

TEST_CASE("the values of a settings file that is read late reach the open window", "[settings][level][editor]") {
    TempSettings temp;
    temp.writeFile(R"({"version": 1, "expert": true, "variationStrength": 77, "gridDivision": 4})");
    auto& store = temp.open(false);
    Quiet quiet;
    Instance instance("Solo");
    std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
    auto* panel = dynamic_cast<mm::plugin::PlaceholderEditor*>(editor.get());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    CHECK_FALSE(panel->expertLevel()); // nothing read yet
    store.beginRead();
    REQUIRE(waitFor([&] { return panel->expertLevel() && panel->variationStrength() == 77; }));
    CHECK(dynamic_cast<juce::ComboBox*>(find(*editor, "grid_box"))->getSelectedId() == 4);
    CHECK(store.ready());
}

TEST_CASE("a voice window has no level switch", "[settings][level][editor]") {
    TempSettings temp;
    temp.writeFile(R"({"version": 1, "expert": true})");
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Quiet quiet;
    Instance voice("Voice");
    auto settings = voice.processor.instanceSettings();
    settings.role = mm::core::InstanceRole::Voice;
    voice.processor.setInstanceSettings(settings);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    REQUIRE(waitFor([&] { return editor->getHeight() == 320; }));
    for (const char* id : {"expert", "seed", "random", "grid_box", "triplet", "snap_box", "vary_all", "strength"}) {
        INFO(id);
        REQUIRE(find(*editor, id) != nullptr);
        CHECK_FALSE(find(*editor, id)->isVisible());
    }
}

namespace {

/// The columns of the image in which the two renderings differ anywhere.
std::vector<int> differingColumns(const juce::Image& a, const juce::Image& b) {
    std::vector<int> columns;
    for (int x = 0; x < a.getWidth(); ++x) {
        for (int y = 0; y < a.getHeight(); ++y) {
            if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) {
                columns.push_back(x);
                break;
            }
        }
    }
    return columns;
}

} // namespace

TEST_CASE("the read-only roll draws the playhead line where the tick is, and nothing without it",
          "[group-wiring][playhead][paint]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::RollView view;
    view.setBounds(0, 0, 800, 120);
    view.setContent(mm::core::layoutRoll({}, 2 * mm::core::kTicksPerBar), "Slot 1 - Bass", false);
    const auto plain = view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f);

    view.setPlayheadTick(1920.0); // a quarter of two bars
    const auto quarter = view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f);
    auto columns = differingColumns(plain, quarter);
    REQUIRE_FALSE(columns.empty());
    CHECK(columns.front() >= 198);
    CHECK(columns.back() <= 202);

    view.setPlayheadTick(5760.0); // three quarters
    columns = differingColumns(plain, view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f));
    REQUIRE_FALSE(columns.empty());
    CHECK(columns.front() >= 598);
    CHECK(columns.back() <= 602);

    view.setPlayheadTick(std::nullopt);
    CHECK(differingColumns(plain, view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f)).empty());

    // an empty slot shows no line, also with a position
    view.setContent({}, "Slot 1 - Bass", true);
    const auto emptyPlain = view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f);
    view.setPlayheadTick(1920.0);
    CHECK(differingColumns(emptyPlain, view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f)).empty());
}

TEST_CASE("the editable roll draws the playhead line at the tick, with the zoom of the window",
          "[group-wiring][playhead][paint]") {
    Quiet quiet;
    RollRig rig;
    const auto plain = rig.roll.createComponentSnapshot(rig.roll.getLocalBounds(), false, 1.0f);
    const auto offsetFor = [&](double tick) {
        rig.roll.setPlayheadTick(tick);
        const auto columns = differingColumns(plain, rig.roll.createComponentSnapshot(rig.roll.getLocalBounds(), false, 1.0f));
        REQUIRE_FALSE(columns.empty());
        CHECK(columns.back() - columns.front() <= 3); // a thin line
        return static_cast<double>(columns.front()) - rig.roll.geometry().tickToX(tick);
    };
    const double first = offsetFor(1920.0);
    const double second = offsetFor(5760.0);
    CHECK(second == Catch::Approx(first).margin(1.5)); // the same distance from the field edge at every tick
    CHECK(first >= 0.0);
    CHECK(first < 60.0); // the key gutter

    rig.roll.setPlayheadTick(std::nullopt);
    CHECK(differingColumns(plain, rig.roll.createComponentSnapshot(rig.roll.getLocalBounds(), false, 1.0f)).empty());
}


namespace {

void writePng(const juce::Image& image, const juce::String& name) {
    const auto folder = juce::SystemStats::getEnvironmentVariable("MM_SNAPSHOT_DIR", {});
    REQUIRE(folder.isNotEmpty());
    const auto file = juce::File(folder).getChildFile(name);
    juce::FileOutputStream stream(file);
    REQUIRE(stream.openedOk());
    stream.setPosition(0);
    stream.truncate();
    juce::PNGImageFormat png;
    REQUIRE(png.writeImageToStream(image, stream));
}

} // namespace

// Renders the windows to PNG files for a look at the design (not a check): MM_SNAPSHOT_DIR=<folder> run with "[.snapshot]".
TEST_CASE("snapshot of the voice window and the hub window", "[.snapshot]") {
    Quiet quiet;
    for (const bool hubWindow : {false, true}) {
        Instance instance("Snapshot");
        instance.setRole(hubWindow ? InstanceRole::Hub : InstanceRole::Voice);
        mm::core::GenerationRequest request;
        request.lengthBars = 4;
        request.seed = 7;
        const auto result = mm::core::generatePattern(*instance.processor.styles().find("peak_time"), request);
        REQUIRE(result.success);
        instance.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, result.pattern)); });
        instance.setSlotParameter(1);
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        instance.setPpq(5.0);
        for (int i = 0; i < 12; ++i) {
            instance.step();
            pumpMessages(40);
        }
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), false, 2.0f);
        writePng(image, hubWindow ? "hub.png" : "voice.png");
    }
}

TEST_CASE("the voice window rows share their columns", "[group-wiring][editor][layout]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* role = child<juce::ComboBox>(*editor, "role");
    auto* voiceBox = child<juce::ComboBox>(*editor, "voice");
    auto* mute = child<juce::ToggleButton>(*editor, "mute");
    auto* octave = child<juce::ComboBox>(*editor, "octave");
    auto* handle = child<mm::plugin::DragHandle>(*editor, "drag");
    auto* generate = child<juce::TextButton>(*editor, "generate");
    auto* takeOver = child<juce::TextButton>(*editor, "takeOver");
    REQUIRE((role && voiceBox && mute && octave && handle && generate && takeOver));
    CHECK(mute->getX() == role->getX());
    CHECK(mute->getWidth() == role->getWidth());
    CHECK(octave->getX() == voiceBox->getX());
    CHECK(octave->getWidth() == voiceBox->getWidth());
    CHECK(child<juce::TextButton>(*editor, "openHub")->getX() == child<juce::ComboBox>(*editor, "follow")->getX());
    CHECK(takeOver->getRight() == generate->getRight()); // the action buttons share the right edge
    CHECK(takeOver->getWidth() == generate->getWidth());
    // the file handle is wide enough to carry its label and spans the first two columns
    CHECK(handle->getX() == role->getX());
    CHECK(handle->getRight() == voiceBox->getRight());
    CHECK(handle->getWidth() >= 120);
}

TEST_CASE("the file handle shows a file icon, and its label when there is room", "[group-wiring][editor][paint]") {
    Quiet quiet;
    Instance voice("Voice");
    mm::plugin::DragHandle handle(voice.processor.midiExporter());
    // light text pixels in the right half of the handle
    const auto textPixels = [&](int width) {
        handle.setBounds(0, 0, width, 40);
        const auto image = handle.createComponentSnapshot(handle.getLocalBounds());
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y) {
            for (int x = width / 2 + 12; x < width - 6; ++x) {
                count += image.getPixelAt(x, y).getBrightness() > 0.8f ? 1 : 0;
            }
        }
        return count;
    };
    const auto iconPixels = [&](int width) { // accent pixels around the centre
        handle.setBounds(0, 0, width, 40);
        const auto image = handle.createComponentSnapshot(handle.getLocalBounds());
        int count = 0;
        for (int y = 0; y < image.getHeight(); ++y) {
            for (int x = 0; x < width; ++x) {
                const auto pixel = image.getPixelAt(x, y);
                count += (pixel.getGreen() > 150 && pixel.getRed() < 120) ? 1 : 0;
            }
        }
        return count;
    };
    CHECK(textPixels(200) > 20); // "MIDI ziehen"
    CHECK(textPixels(60) == 0);  // icon only
    CHECK(iconPixels(60) > 100);  // the icon is there in both
    CHECK(iconPixels(200) > 100);
}

TEST_CASE("many drops in a running hub with voices never lose a pattern, and the undo button brings each one back",
          "[group-wiring][slot-drop]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance bass("Bass");
    Instance lead("Lead");
    hub.setRole(InstanceRole::Hub);
    bass.setRole(InstanceRole::Voice);
    lead.setRole(InstanceRole::Voice);
    hub.processor.editSlots([](mm::core::SlotBank& bank) {
        for (size_t slot = 0; slot < 5; ++slot) {
            REQUIRE(bank.setResult(slot, marked(static_cast<uint8_t>(41 + slot))));
            REQUIRE(bank.setResult(slot, marked(static_cast<uint8_t>(61 + slot)))); // a history of two
        }
    });
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    double ppq = 4.0;
    runTo({&hub, &bass, &lead}, ppq);
    const auto pitches = [&] {
        std::vector<int> found;
        const auto bank = hub.processor.slotsSnapshot();
        for (size_t slot = 0; slot < mm::core::kSlotCount; ++slot) {
            if (bank.slot(slot)->pattern) {
                found.push_back(bank.slot(slot)->pattern->voices[0].notes[0].pitch);
            }
        }
        std::sort(found.begin(), found.end());
        return found;
    };
    const auto before = pitches();
    REQUIRE(before.size() == 5);
    const auto layout = [&] { // the pitch of every slot, 0 for an empty one
        std::vector<int> found;
        const auto bank = hub.processor.slotsSnapshot();
        for (size_t slot = 0; slot < mm::core::kSlotCount; ++slot) {
            found.push_back(bank.slot(slot)->pattern ? bank.slot(slot)->pattern->voices[0].notes[0].pitch : 0);
        }
        return found;
    };
    const auto initial = layout();
    const int moves[][2] = {{1, 2}, {2, 7}, {7, 1}, {3, 4}, {5, 5 + 1}, {1, 9}, {9, 2}, {4, 16}, {16, 3}};
    for (const auto& move : moves) {
        auto* from = child<mm::plugin::SlotButton>(*editor, "slot" + juce::String(move[0]));
        auto* to = child<mm::plugin::SlotButton>(*editor, "slot" + juce::String(move[1]));
        to->itemDropped(juce::DragAndDropTarget::SourceDetails(mm::plugin::SlotButton::describe(move[0]), from, {}));
        ppq += 2.0;
        runTo({&hub, &bass, &lead}, ppq);
        pumpMessages(20);
        INFO(move[0] << " -> " << move[1]);
        CHECK(pitches() == before);
    }

    // every drop is a step: the undo button takes them back one by one, up to the first arrangement
    auto* undo = child<juce::TextButton>(*editor, "undo");
    REQUIRE(undo != nullptr);
    int steps = 0;
    while (hub.processor.canUndo() && steps < 20) {
        REQUIRE(waitFor([&] { return undo->isEnabled(); }));
        undo->onClick();
        ++steps;
        CHECK(pitches() == before); // never a lost pattern on the way
    }
    CHECK(steps == 9);
    CHECK(layout() == initial);
}

TEST_CASE("after a drop the rows show the patterns that are in the slots now", "[group-wiring][slot-drop]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([](mm::core::SlotBank& bank) {
        REQUIRE(bank.setResult(0, marked(41)));                         // one bar
        REQUIRE(bank.setResult(1, mm::core::makeEmptyPattern(2, "peak_time"))); // two bars, no notes
    });
    hub.setSlotParameter(2);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    hub.step();
    pumpMessages(80);
    auto* strip1 = child<mm::plugin::SlotButton>(*editor, "slot1");
    auto* strip2 = child<mm::plugin::SlotButton>(*editor, "slot2");
    auto rowTicks = [&] {
        for (auto* component : editor->getChildren()) {
            if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                return row->roll().patternTicks();
            }
        }
        return uint32_t{9999};
    };
    CHECK(rowTicks() == 2 * 3840);
    strip2->itemDropped(juce::DragAndDropTarget::SourceDetails(mm::plugin::SlotButton::describe(1), strip1, {}));
    hub.step();
    pumpMessages(120);
    CHECK(hub.processor.slotsSnapshot().slot(1)->pattern->lengthBars == 1);
    CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->lengthBars == 2);
    CHECK(rowTicks() == 3840);
}

TEST_CASE("a slot chosen while the host does not run shows its pattern at once", "[group-wiring][slot-drop]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([](mm::core::SlotBank& bank) {
        REQUIRE(bank.setResult(0, marked(41)));
        REQUIRE(bank.setResult(1, marked(42)));
    });
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    hub.step(); // the audio thread reports slot 1 as the playing one
    auto* info = dynamic_cast<juce::Label*>(find(*editor, "info"));
    REQUIRE(info != nullptr);
    REQUIRE(waitFor([&] { return info->getText().contains("Slot 1"); }));

    // the host stops and no longer calls processBlock (Live does that for a silent device): nothing reports slot 2
    hub.setPlaying(false);
    hub.step();
    FakePlayheadClock::advance(5.0);
    hub.setSlotParameter(2);
    CHECK(waitFor([&] { return info->getText().contains("Slot 2"); }));
}

TEST_CASE("a drag of another slot with Alt held shows the copy cursor", "[group-wiring][slot-drop]") {
    using mm::plugin::SlotButton;
    const auto from2 = SlotButton::describe(2);
    CHECK(SlotButton::showsCopy(from2, 5, true));
    CHECK_FALSE(SlotButton::showsCopy(from2, 5, false)); // an exchange
    CHECK_FALSE(SlotButton::showsCopy(from2, 2, true));  // the field itself
    CHECK_FALSE(SlotButton::showsCopy(juce::var("something else"), 5, true));
}

TEST_CASE("the title of a roll lies above the notes, never over them", "[roll][editor][layout]") {
    RollRig rig;
    const auto title = mm::plugin::EditableRoll::kTitleHeight;
    const auto field = rig.roll.getLocalBounds().withTrimmedTop(title);
    CHECK(title >= 16);
    // a note of the lowest/highest row is still drawn below the strip: a click in the strip hits nothing
    const juce::Point<float> inStrip(static_cast<float>(mm::plugin::EditableRoll::kGutter + 40), static_cast<float>(title) - 4.0f);
    rig.roll.mouseDown(mouseEventAt(rig.roll, inStrip));
    rig.roll.mouseUp(mouseEventAt(rig.roll, inStrip));
    CHECK(rig.roll.selection().empty());
    CHECK(rig.roll.geometry().height == field.getHeight() - static_cast<int>(kLane));
}

TEST_CASE("the read-only roll keeps a strip for the title: a note in the top row does not reach it", "[group-wiring][paint][layout]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::RollView view;
    view.setBounds(0, 0, 400, 160);
    mm::core::RollLayout layout;
    layout.bars = 1;
    layout.notes.push_back({0.3f, 0.0f, 0.3f, 0.1f, 127}); // the highest row, in the middle of the roll
    view.setContent(layout, "Slot 1 - Bass", false);
    const auto image = view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f);
    const auto accentInRows = [&](int from, int to) {
        int count = 0;
        for (int y = from; y < to; ++y) {
            for (int x = 40; x < image.getWidth() - 40; ++x) {
                const auto pixel = image.getPixelAt(x, y);
                count += (pixel.getGreen() > 150 && pixel.getRed() < 120) ? 1 : 0;
            }
        }
        return count;
    };
    CHECK(accentInRows(0, mm::plugin::RollView::kTitleHeight) == 0);
    CHECK(accentInRows(mm::plugin::RollView::kTitleHeight, mm::plugin::RollView::kTitleHeight + 30) > 100);
}

TEST_CASE("a voice that was switched to Hub next to a hub can be switched back to Voice", "[group-wiring][role]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    pumpMessages(20);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    pumpMessages(40);
    auto* role = child<juce::ComboBox>(*editor, "role");
    REQUIRE(role != nullptr);
    role->setSelectedId(2, juce::sendNotificationSync); // Hub, but there is one already
    pumpMessages(60);
    INFO("status " << static_cast<int>(voice.processor.groupStatus()) << " role "
                   << static_cast<int>(voice.processor.instanceSettings().role));
    role = child<juce::ComboBox>(*editor, "role");
    REQUIRE(role != nullptr);
    CHECK(role->isVisible());
    CHECK(role->isEnabled());
    role->setSelectedId(3, juce::sendNotificationSync);
    pumpMessages(60);
    CHECK(voice.processor.instanceSettings().role == InstanceRole::Voice);
    CHECK(voice.processor.groupStatus() == mm::core::GroupStatus::VoiceConnected);
}

TEST_CASE("the role box of the hub window is wide enough and covered by nothing", "[group-wiring][role][layout]") {
    Quiet quiet;
    for (const auto role : {InstanceRole::Solo, InstanceRole::Hub}) {
        Instance instance("Test");
        instance.setRole(role);
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        pumpMessages(40);
        auto* box = child<juce::ComboBox>(*editor, "role");
        REQUIRE(box != nullptr);
        INFO("role " << static_cast<int>(role));
        CHECK(box->getWidth() >= 100); // it was 0 px wide once the help button took its room
        CHECK(editor->getLocalBounds().contains(box->getBounds()));
        for (auto* other : editor->getChildren()) {
            if (other != box && other->isVisible() && other->getWidth() > 0) {
                INFO(other->getComponentID().toStdString());
                CHECK_FALSE(box->getBounds().intersects(other->getBounds()));
            }
        }
    }
}

TEST_CASE("a click on the Open hub button reaches it, also when the window is scaled", "[group-wiring][editor][layout]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* button = child<juce::TextButton>(*editor, "openHub");
    REQUIRE(button != nullptr);
    editor->setVisible(true); // a component that is not shown is not hit
    REQUIRE(waitFor([&] { return button->isEnabled() && button->isVisible(); }));
    for (const int pct : {100, 150, 75}) {
        voice.processor.setUiScalePct(pct);
        pumpMessages(40);
        INFO("scale " << pct);
        const auto centre = button->getBounds().getCentre();
        auto* hit = editor->getComponentAt(centre);
        INFO("hit " << (hit != nullptr ? hit->getComponentID().toStdString() : std::string("none")) << " w "
                    << button->getWidth() << " h " << button->getHeight());
        CHECK(hit == button);
    }
}

TEST_CASE("Open hub works again after the voice was set to Hub and back", "[group-wiring][editor][role]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    pumpMessages(40);
    auto* role = child<juce::ComboBox>(*editor, "role");
    role->setSelectedId(2, juce::sendNotificationSync); // refused: there is a hub
    pumpMessages(40);
    role = child<juce::ComboBox>(*editor, "role");
    role->setSelectedId(3, juce::sendNotificationSync);
    pumpMessages(40);
    auto* button = child<juce::TextButton>(*editor, "openHub");
    REQUIRE(button != nullptr);
    REQUIRE(waitFor([&] { return button->isEnabled(); }));
    button->triggerClick();
    CHECK(waitFor([&] { return hub.processor.frontRequests() == 1; }));
}

TEST_CASE("Open hub says what to do when the window of the hub is closed", "[group-wiring][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* button = child<juce::TextButton>(*editor, "openHub");
    REQUIRE(button != nullptr);
    REQUIRE(waitFor([&] { return button->isEnabled(); }));
    auto* status = dynamic_cast<juce::Label*>(find(*editor, "status"));
    REQUIRE(status != nullptr);

    voice.processor.showHub(); // the hub has no editor
    CHECK(waitFor([&] { return voice.processor.generationStatus() == mm::plugin::GenerationStatus::HubWindowClosed; }));
    CHECK(waitFor([&] { return status->getText().contains("Fenster des Hubs ist zu"); }));

    // with the window of the hub open the request is done and the message stays away
    std::unique_ptr<juce::AudioProcessorEditor> hubEditor(hub.processor.createEditorAndMakeActive());
    REQUIRE(hubEditor != nullptr);
    voice.processor.noteHubWindowClosed(); // anything but the closed state, then ask again
    voice.processor.generate();
    voice.processor.showHub();
    pumpMessages(60);
    CHECK(voice.processor.generationStatus() != mm::plugin::GenerationStatus::HubWindowClosed);
    CHECK(hub.processor.frontRequests() >= 1);
    hub.processor.editorBeingDeleted(hubEditor.get()); // what the host wrapper does
    hubEditor.reset();
}

TEST_CASE("hub and voice offer the same automatable mute parameters to the host", "[group-wiring][params]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    pumpMessages(20);
    for (Instance* instance : {&hub, &voice}) {
        for (int n = 1; n <= 8; ++n) {
            auto* parameter = instance->processor.parameters().getParameter("mute_" + juce::String(n));
            INFO("mute " << n);
            REQUIRE(parameter != nullptr);
            CHECK(parameter->isAutomatable());
            CHECK(parameter->getName(64) == "Mute " + juce::String(n));
            CHECK(parameter->isDiscrete());
        }
    }
    CHECK(hub.processor.getParameters().size() == voice.processor.getParameters().size());
}

TEST_CASE("the rolls of the hub share their time window, the pitch rows stay each its own", "[group-wiring][editor][follow]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots(
        [](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, mm::core::makeEmptyPattern(8, "peak_time"))); });
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    const auto rolls = [&] {
        std::vector<mm::plugin::EditableRoll*> found;
        for (auto* component : editor->getChildren()) {
            if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                found.push_back(&row->roll());
            }
        }
        return found;
    };
    REQUIRE(waitFor([&] { return rolls().size() >= 2 && rolls()[0]->patternTicks() == 8 * 3840; }));
    auto all = rolls();
    REQUIRE(all[1]->patternTicks() == 8 * 3840);
    CHECK(all[0]->viewport().spanTicks == all[1]->viewport().spanTicks);

    // zooming one roll in time zooms the other the same way
    all[0]->zoomAt(0.0, 0.25);
    CHECK(all[0]->viewport().spanTicks < 4 * 3840);
    CHECK(all[1]->viewport().spanTicks == all[0]->viewport().spanTicks);
    CHECK(all[1]->viewport().startTick == all[0]->viewport().startTick);

    // scrolling in time too, from either roll
    all[1]->scrollBy(3840, 0);
    CHECK(all[0]->viewport().startTick == all[1]->viewport().startTick);
    CHECK(all[0]->viewport().startTick > 0);

    // the pitch rows do not go along
    const int lowFirst = all[0]->viewport().lowPitch;
    const int lowSecond = all[1]->viewport().lowPitch;
    all[1]->scrollBy(0, 5);
    CHECK(all[1]->viewport().lowPitch == lowSecond + 5);
    CHECK(all[0]->viewport().lowPitch == lowFirst);
}

TEST_CASE("the rolls follow the playhead page by page while Follow is on", "[group-wiring][editor][follow]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots(
        [](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, mm::core::makeEmptyPattern(8, "peak_time"))); });
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    const auto rolls = [&] {
        std::vector<mm::plugin::EditableRoll*> found;
        for (auto* component : editor->getChildren()) {
            if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                found.push_back(&row->roll());
            }
        }
        return found;
    };
    const auto drive = [&](int steps) {
        for (int i = 0; i < steps; ++i) {
            hub.step();
            pumpMessages(20);
        }
    };
    hub.setPpq(0.5);
    REQUIRE(waitFor([&] { hub.step(); return rolls().size() >= 2 && rolls()[0]->patternTicks() == 8 * 3840; }));
    rolls()[0]->zoomAt(0.0, 0.0001); // as narrow as allowed (one quarter note) ...
    rolls()[0]->zoomAt(0.0, 4.0);    // ... and four times as wide: one bar
    CHECK(rolls()[1]->viewport().spanTicks == 3840);
    auto* button = child<juce::TextButton>(*editor, "follow_playhead");
    REQUIRE(button != nullptr);
    CHECK_FALSE(hub.processor.followPlayhead());

    // Follow off: the playhead leaves the window and the window stays
    hub.setPpq(5.0); // tick 4800: the second bar
    drive(10);
    CHECK(rolls()[0]->viewport().startTick == 0);

    // Follow on: both rolls turn to the page of the playhead
    button->setToggleState(true, juce::sendNotificationSync);
    CHECK(hub.processor.followPlayhead());
    hub.setPpq(5.0);
    drive(10);
    CHECK(waitFor([&] { return rolls()[0]->viewport().startTick == 3840 && rolls()[1]->viewport().startTick == 3840; }));
    hub.setPpq(13.0); // tick 12480: the fourth bar
    drive(10);
    CHECK(waitFor([&] { return rolls()[0]->viewport().startTick == 11520 && rolls()[1]->viewport().startTick == 11520; }));
    CHECK(rolls()[0]->viewport().spanTicks == 3840); // the zoom stays

    // and off again
    button->setToggleState(false, juce::sendNotificationSync);
    CHECK_FALSE(hub.processor.followPlayhead());
    hub.setPpq(1.0);
    drive(10);
    CHECK(rolls()[0]->viewport().startTick == 11520);
}

TEST_CASE("the roll of the voice window zooms, scrolls and follows the playhead", "[group-wiring][editor][follow]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    voice.processor.editSlots(
        [](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, mm::core::makeEmptyPattern(8, "peak_time"))); });
    voice.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    auto* roll = child<mm::plugin::RollView>(*editor, "roll");
    REQUIRE(roll != nullptr);
    REQUIRE(waitFor([&] { return roll->patternTicks() == 8 * 3840; }));
    CHECK(roll->viewSpan() == 8 * 3840); // the whole pattern at first

    roll->zoomAt(0.0, 0.125);
    CHECK(roll->viewSpan() == 3840);
    roll->scrollBy(1000);
    CHECK(roll->viewStart() == 1000);

    auto* button = child<juce::TextButton>(*editor, "follow_playhead");
    REQUIRE(button != nullptr);
    voice.setPpq(9.0); // tick 8640: the third bar
    for (int i = 0; i < 10; ++i) {
        voice.step();
        pumpMessages(20);
    }
    CHECK(roll->viewStart() == 1000); // off: nothing moves
    button->setToggleState(true, juce::sendNotificationSync);
    voice.setPpq(9.0);
    for (int i = 0; i < 10; ++i) {
        voice.step();
        pumpMessages(20);
    }
    CHECK(waitFor([&] { return roll->viewStart() == 7680; })); // the page of tick 8640 with a width of one bar: bar 3
    CHECK(roll->viewSpan() == 3840);
}

TEST_CASE("the follow switch is saved with the instance and only when it is on", "[group-wiring][follow]") {
    Quiet quiet;
    Instance source("Source");
    CHECK_FALSE(source.processor.followPlayhead());
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("followPlayhead")); // older states stay as they were

    source.processor.setFollowPlayhead(true);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.followPlayhead());
    target.processor.setStateInformation(standard.getData(), static_cast<int>(standard.getSize()));
    CHECK_FALSE(target.processor.followPlayhead());
}

TEST_CASE("the follow button is in the hub and in the voice window and overlaps nothing", "[group-wiring][editor][layout][follow]") {
    Quiet quiet;
    for (const auto role : {InstanceRole::Hub, InstanceRole::Voice}) {
        Instance instance("Test");
        instance.setRole(role);
        std::unique_ptr<juce::AudioProcessorEditor> editor(instance.processor.createEditor());
        pumpMessages(40);
        auto* button = child<juce::TextButton>(*editor, "follow_playhead");
        REQUIRE(button != nullptr);
        INFO("role " << static_cast<int>(role));
        CHECK(button->isVisible());
        CHECK(button->getButtonText() == "Folgen");
        CHECK(button->getWidth() >= 80);
        CHECK(editor->getLocalBounds().contains(button->getBounds()));
        for (auto* other : editor->getChildren()) {
            if (other != button && other->isVisible() && other->getWidth() > 0) {
                INFO(other->getComponentID().toStdString());
                CHECK_FALSE(button->getBounds().intersects(other->getBounds()));
            }
        }
    }
}

TEST_CASE("the read-only roll draws only the visible part of the pattern, notes where the window puts them",
          "[group-wiring][paint][follow]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::RollView view;
    view.setBounds(0, 0, 400, 160);
    mm::core::RollLayout layout;
    layout.bars = 2;
    layout.notes.push_back({0.5f, 0.5f, 0.1f, 0.1f, 127}); // tick 3840, the start of the second bar
    view.setContent(layout, "Slot 1 - Bass", false);
    const auto accentColumns = [&] {
        const auto image = view.createComponentSnapshot(view.getLocalBounds(), false, 1.0f);
        std::vector<int> columns;
        for (int x = 0; x < image.getWidth(); ++x) {
            for (int y = mm::plugin::RollView::kTitleHeight; y < image.getHeight(); ++y) {
                const auto pixel = image.getPixelAt(x, y);
                if (pixel.getGreen() > 150 && pixel.getRed() < 120) {
                    columns.push_back(x);
                    break;
                }
            }
        }
        return columns;
    };
    auto columns = accentColumns(); // the whole pattern: the note sits in the middle
    REQUIRE_FALSE(columns.empty());
    CHECK(columns.front() >= 195);
    CHECK(columns.front() <= 205);

    view.setTimeView(3840, 3840); // the second bar: the note is at the left edge and twice as wide as before
    columns = accentColumns();
    REQUIRE_FALSE(columns.empty());
    CHECK(columns.front() <= 5);
    CHECK(columns.back() - columns.front() >= 70);

    view.setTimeView(0, 3840); // the first bar: no note in view
    CHECK(accentColumns().empty());
}

TEST_CASE("a sideways swipe moves the content with the fingers, in the roll of the hub and of the voice",
          "[group-wiring][follow][scroll]") {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::MouseWheelDetails right; // fingers to the right: the content goes right, the window to earlier ticks
    right.deltaX = 0.2f;
    right.deltaY = 0.0f;
    auto left = right;
    left.deltaX = -0.2f;

    mm::plugin::RollView view;
    view.setBounds(0, 0, 400, 160);
    view.setContent(mm::core::layoutRoll({}, 8 * mm::core::kTicksPerBar), "Slot 1 - Bass", false);
    view.setTimeView(3 * mm::core::kTicksPerBar, mm::core::kTicksPerBar);
    const auto event = [](juce::Component& component) { return mouseEventAt(component, {100.0f, 100.0f}); };
    view.mouseWheelMove(event(view), right);
    CHECK(view.viewStart() < 3 * mm::core::kTicksPerBar);
    view.setTimeView(3 * mm::core::kTicksPerBar, mm::core::kTicksPerBar);
    view.mouseWheelMove(event(view), left);
    CHECK(view.viewStart() > 3 * mm::core::kTicksPerBar);

    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots(
        [](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, mm::core::makeEmptyPattern(8, "peak_time"))); });
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    mm::plugin::EditableRoll* roll = nullptr;
    REQUIRE(waitFor([&] {
        for (auto* component : editor->getChildren()) {
            if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                roll = &row->roll();
                return roll->patternTicks() == 8 * mm::core::kTicksPerBar;
            }
        }
        return false;
    }));
    roll->zoomAt(0.0, 0.25);
    roll->scrollBy(3 * mm::core::kTicksPerBar, 0);
    const auto before = roll->viewport().startTick;
    REQUIRE(before > 0);
    roll->mouseWheelMove(event(*roll), right);
    CHECK(roll->viewport().startTick < before);
    const auto earlier = roll->viewport().startTick;
    roll->mouseWheelMove(event(*roll), left);
    CHECK(roll->viewport().startTick > earlier);
}

TEST_CASE("the roll scrolls in the pitch by parts of a row and a note is hit where it is drawn", "[roll][editor][smooth]") {
    RollRig rig;
    juce::MouseWheelDetails tiny;
    tiny.deltaX = 0.0f;
    tiny.deltaY = 0.02f; // a trackpad step of 0.16 of a row
    const auto yBefore = rig.roll.geometry().pitchToY(64);
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, {100.0f, 100.0f}), tiny);
    CHECK(rig.roll.viewport().pitchOffset == Catch::Approx(0.16).margin(1e-6)); // moved at once, not after a whole row
    CHECK(rig.roll.geometry().pitchToY(64) > yBefore);                            // the notes slid down a little
    CHECK(rig.roll.geometry().pitchToY(64) - yBefore < rig.roll.geometry().rowHeight());

    // a click on the note where it is now drawn selects it
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, {100.0f, 100.0f}), tiny);
    rig.refresh();
    const auto onNote = rig.at(960, 64);
    const auto position = juce::Point<float>(static_cast<float>(onNote.x) + mm::plugin::EditableRoll::kGutter,
                                             static_cast<float>(onNote.y) + mm::plugin::EditableRoll::kTitleHeight + 2.0f);
    rig.roll.mouseDown(mouseEventAt(rig.roll, position));
    rig.roll.mouseUp(mouseEventAt(rig.roll, position));
    CHECK(rig.roll.selection() == std::set<uint32_t>{rig.idAt(1)});

    // the other way: back to where it was, the offset ends at zero, not below it
    juce::MouseWheelDetails back = tiny;
    back.deltaY = -1.0f;
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, {100.0f, 100.0f}), back);
    rig.roll.mouseWheelMove(mouseEventAt(rig.roll, {100.0f, 100.0f}), back);
    CHECK(rig.roll.viewport().pitchOffset >= 0.0);
    CHECK(rig.roll.viewport().pitchOffset < 1.0);
}

TEST_CASE("pinching zooms the roll of the hub and of the voice window, and the rolls of the hub together",
          "[group-wiring][follow][pinch]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::RollView view;
    view.setBounds(0, 0, 400, 160);
    view.setContent(mm::core::layoutRoll({}, 8 * mm::core::kTicksPerBar), "Slot 1 - Bass", false);
    const auto event = [](juce::Component& component) { return mouseEventAt(component, {100.0f, 100.0f}); };
    const auto whole = view.viewSpan();
    view.mouseMagnify(event(view), 2.0f); // fingers apart: closer
    CHECK(view.viewSpan() < whole);
    const auto closer = view.viewSpan();
    view.mouseMagnify(event(view), 0.5f); // together: further away
    CHECK(view.viewSpan() > closer);
    view.mouseMagnify(event(view), 0.0f); // nonsense changes nothing
    view.mouseMagnify(event(view), -1.0f);

    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots(
        [](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, mm::core::makeEmptyPattern(8, "peak_time"))); });
    hub.setSlotParameter(1);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    std::vector<mm::plugin::EditableRoll*> rolls;
    REQUIRE(waitFor([&] {
        rolls.clear();
        for (auto* component : editor->getChildren()) {
            if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                rolls.push_back(&row->roll());
            }
        }
        return rolls.size() >= 2 && rolls[0]->patternTicks() == 8 * mm::core::kTicksPerBar;
    }));
    const auto before = rolls[0]->viewport().spanTicks;
    rolls[0]->mouseMagnify(event(*rolls[0]), 2.0f);
    CHECK(rolls[0]->viewport().spanTicks < before);
    CHECK(rolls[1]->viewport().spanTicks == rolls[0]->viewport().spanTicks);
}

namespace {

/// A generated two-bar pattern of the shipped styles for the Evolve tests (8 PPQ per pass).
mm::core::Pattern evolvePattern(Instance& hub, uint64_t seed) {
    const auto* style = hub.processor.styles().findOrFallback("peak_time");
    REQUIRE(style != nullptr);
    mm::core::GenerationRequest request;
    request.lengthBars = 2;
    request.seed = seed;
    const auto result = mm::core::generatePattern(*style, request);
    REQUIRE(result.success);
    return result.pattern;
}

/// What the instance sounded between two PPQ positions, in order: pitch and the time from the start of each note on, so
/// that a stage that moves a note is told from the stage before (the velocity is left out: the output stage varies it
/// from pass to pass).
std::vector<long long> pitchesBetween(const Instance& instance, double fromPpq, double toPpq) {
    std::vector<long long> sound;
    for (const auto& note : instance.notes()) {
        if (note.sample >= at(fromPpq) && note.sample < at(toPpq)) {
            sound.push_back(note.pitch);
            sound.push_back((note.sample - at(fromPpq) + 500) / 1000); // a block's rounding is no change
        }
    }
    return sound;
}

/// Drives the hub block by block up to `ppq` and calls the timer of the Evolve layer after each block.
void runEvolve(Instance& hub, double ppq) {
    while (hub.ppq() < ppq - 1e-9) {
        hub.step();
        hub.processor.evolveTick();
    }
}

/// The notes of both patterns are the same (the bank gives every stored pattern its own version).
bool sameNotes(const mm::core::Pattern& a, const mm::core::Pattern& b) {
    return a.voices == b.voices;
}

mm::core::EvolveSettings evolveEvery(int passes, bool fromOriginal = false, int strength = 40) {
    return {passes, fromOriginal, strength};
}

} // namespace

TEST_CASE("evolve: a new stage every N passes plays from the pass after, the slot stays as it is", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    const mm::core::Pattern original = evolvePattern(hub, 1);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    hub.processor.setEvolveSettings(evolveEvery(2));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    REQUIRE(hub.processor.evolveOn());

    runEvolve(hub, 8.0 * 6 + 0.1);
    // Passes of 8 PPQ: 1 and 2 are stage 0, 3 and 4 stage 1 (made at the start of pass 2), 5 and 6 stage 2.
    const auto pass = [&](int n) { return pitchesBetween(hub, 8.0 * n, 8.0 * (n + 1)); };
    REQUIRE_FALSE(pass(0).empty());
    CHECK(pass(0) == pass(1));
    CHECK(pass(2) != pass(1));
    CHECK(pass(2) == pass(3));
    CHECK(pass(4) != pass(3));
    CHECK(pass(4) == pass(5));
    CHECK(hub.processor.evolveInfo().stage == 3); // stage 3 was made at the start of pass 6 for pass 7
    CHECK(hub.processor.evolveInfo().changed);
    // The slot itself never changed (SPEC 6.7)
    const auto bank = hub.processor.slotsSnapshot();
    CHECK(sameNotes(*bank.slot(0)->pattern, original));
    CHECK(bank.slot(0)->history.size() == 1);
}

TEST_CASE("evolve: nothing happens while the transport stands, and counting starts again with the next start",
          "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 2))); });
    hub.processor.setEvolveSettings(evolveEvery(1));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    hub.setPlaying(false);
    for (int i = 0; i < 40; ++i) {
        hub.step();
        hub.processor.evolveTick();
    }
    CHECK(hub.processor.evolveInfo().stage == 0);
    CHECK(hub.processor.evolveInfo().layerActive);
    hub.setPlaying(true);
    runEvolve(hub, 3.9);
    CHECK(hub.processor.evolveInfo().stage == 0); // not yet: the stage is made in the middle of the pass (8 PPQ)
    runEvolve(hub, 4.2);
    CHECK(hub.processor.evolveInfo().stage == 1); // every pass: the first pass already prepares the next stage, in its middle
}

TEST_CASE("evolve: from the original every stage varies the slot, not the stage before", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    for (const bool fromOriginal : {false, true}) {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        const mm::core::Pattern original = evolvePattern(hub, 3);
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
        hub.processor.setEvolveSettings(evolveEvery(1, fromOriginal, 30));
        hub.processor.setEvolveSeed(7);
        hub.processor.setEvolve(true);
        runEvolve(hub, 8.0 * 8 + 0.1);
        INFO("fromOriginal " << fromOriginal);
        CHECK(hub.processor.evolveInfo().stage >= 7);
        CHECK(sameNotes(*hub.processor.slotsSnapshot().slot(0)->pattern, original));
    }
}

TEST_CASE("evolve: keep takes the stage into the slot as a history entry and an undo step, and Evolve goes on",
          "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    const mm::core::Pattern original = evolvePattern(hub, 4);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    hub.processor.setEvolveSettings(evolveEvery(1));
    hub.processor.setEvolveSeed(7);
    CHECK_FALSE(hub.processor.evolveKeep()); // off: nothing to keep
    CHECK(hub.processor.generationStatus() == mm::plugin::GenerationStatus::EvolveNothingToKeep);
    hub.processor.setEvolve(true);
    CHECK_FALSE(hub.processor.evolveKeep()); // stage 0 is the slot itself
    runEvolve(hub, 8.0 * 3 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);
    REQUIRE(hub.processor.evolveKeep());
    CHECK(hub.processor.generationStatus() == mm::plugin::GenerationStatus::EvolveKept);
    const auto bank = hub.processor.slotsSnapshot();
    CHECK_FALSE(sameNotes(*bank.slot(0)->pattern, original));
    CHECK(bank.slot(0)->history.size() == 2);
    CHECK(hub.processor.canUndo());
    const auto info = hub.processor.evolveInfo();
    CHECK(info.on);
    CHECK(info.layerActive);
    CHECK(info.stage == 0); // goes on from the kept stage
    CHECK_FALSE(info.changed);
    runEvolve(hub, 8.0 * 5 + 0.1);
    CHECK(hub.processor.evolveInfo().stage >= 1);
    CHECK(sameNotes(*hub.processor.slotsSnapshot().slot(0)->pattern, *bank.slot(0)->pattern)); // the slot stays at the kept stage
    // one undo step takes the kept stage back
    REQUIRE(hub.processor.undo());
    CHECK(sameNotes(*hub.processor.slotsSnapshot().slot(0)->pattern, original));
}

TEST_CASE("evolve: back plays one of the last stages again, no further than 8", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 5))); });
    hub.processor.setEvolveSettings(evolveEvery(1));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    CHECK_FALSE(hub.processor.evolveBack());
    runEvolve(hub, 8.0 * 4 + 0.1);
    REQUIRE(hub.processor.evolveInfo().canBack);
    const auto before = pitchesBetween(hub, 8.0 * 2, 8.0 * 3);
    REQUIRE(hub.processor.evolveBack());
    runEvolve(hub, 8.0 * 6 + 0.1);
    // after going back, the layer goes on from the stage that sounds: the stage numbers keep counting
    CHECK(hub.processor.evolveInfo().stage >= 3);
    CHECK_FALSE(before.empty());
}

TEST_CASE("evolve: switching off plays the unchanged slot again from the next bar line", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 6))); });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 60));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolve(hub, 8.0 * 3 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);
    const auto stage = pitchesBetween(hub, 8.0 * 2, 8.0 * 2 + 4.0); // first bar of the pass that plays stage 2
    hub.processor.setEvolve(false);
    CHECK_FALSE(hub.processor.evolveOn());
    CHECK_FALSE(hub.processor.evolveInfo().layerActive);
    runEvolve(hub, 8.0 * 5);
    // The first pass after the switch-off is the slot again: it equals the very first pass
    const auto slotAgain = pitchesBetween(hub, 8.0 * 4, 8.0 * 4 + 4.0);
    CHECK(slotAgain == pitchesBetween(hub, 0.0, 4.0));
    CHECK(slotAgain != stage);
}

TEST_CASE("evolve: a slot change ends the layer of the old slot without taking it over", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    const mm::core::Pattern first = evolvePattern(hub, 7);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) {
        REQUIRE(bank.setResult(0, first));
        REQUIRE(bank.setResult(1, evolvePattern(hub, 8)));
    });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 60));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolve(hub, 8.0 * 3 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);
    hub.setSlotParameter(2);
    runEvolve(hub, 8.0 * 4 + 0.1);
    CHECK(hub.processor.evolveInfo().layerActive); // a new layer on slot 2
    CHECK(sameNotes(*hub.processor.slotsSnapshot().slot(0)->pattern, first));
    CHECK(hub.processor.slotsSnapshot().slot(0)->history.size() == 1);
    CHECK(hub.processor.evolveInfo().stage <= 2);
    // Back to slot 1: it plays its own content, not the stage the old layer left in the engine
    hub.processor.setEvolve(false);
    hub.setSlotParameter(1);
    runEvolve(hub, 8.0 * 7);
    CHECK(pitchesBetween(hub, 8.0 * 6, 8.0 * 6 + 4.0) == pitchesBetween(hub, 0.0, 4.0));
}

TEST_CASE("evolve: a manual action works on the stage that sounds, nothing the musician heard is lost",
          "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    const mm::core::Pattern original = evolvePattern(hub, 9);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 60));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolve(hub, 8.0 * 3 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);
    // A note edit
    REQUIRE(hub.processor.editNotes(0, [](mm::core::Pattern& pattern) {
        return mm::core::setVoiceLocked(pattern, 0, true);
    }));
    const auto bank = hub.processor.slotsSnapshot();
    CHECK(bank.slot(0)->history.size() == 2);  // the stage became a result first
    CHECK_FALSE(sameNotes(*bank.slot(0)->pattern, original));
    CHECK(hub.processor.evolveInfo().layerActive);
    CHECK_FALSE(hub.processor.evolveInfo().changed); // and the layer goes on from there
    // undo: the edit, then the kept stage
    REQUIRE(hub.processor.undo());
    REQUIRE(hub.processor.undo());
    CHECK(sameNotes(*hub.processor.slotsSnapshot().slot(0)->pattern, original));
}

TEST_CASE("evolve: a manual result restarts the layer from the new slot content", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 10))); });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 60));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolve(hub, 8.0 * 3 + 0.1);
    REQUIRE(hub.processor.evolveInfo().stage > 0);
    const mm::core::Pattern fresh = evolvePattern(hub, 11);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, fresh)); });
    hub.setPlaying(false); // the tick restarts the layer without counting a pass
    hub.step();
    hub.processor.evolveTick();
    CHECK(hub.processor.evolveInfo().stage == 0); // the stage was dropped: the manual result wins (SPEC 6.8)
    CHECK_FALSE(hub.processor.evolveInfo().changed);
    CHECK(sameNotes(*hub.processor.slotsSnapshot().slot(0)->pattern, fresh));
}

TEST_CASE("evolve: an offline render switches it off, a voice cannot switch it on", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 12))); });
        hub.processor.setEvolveSettings(evolveEvery(1, false, 60));
        hub.processor.setEvolveSeed(7);
        hub.processor.setEvolve(true);
        runEvolve(hub, 8.0 * 3 + 0.1);
        REQUIRE(hub.processor.evolveInfo().changed);
        hub.processor.setNonRealtime(true);
        hub.processor.evolveTick();
        CHECK_FALSE(hub.processor.evolveOn());
        CHECK_FALSE(hub.processor.evolveInfo().layerActive);
        hub.processor.setNonRealtime(false);
        hub.processor.setEvolve(false); // takes the idle timer away
    }
    {
        Instance voice("Voice");
        voice.setRole(InstanceRole::Voice);
        voice.processor.setEvolve(true);
        CHECK_FALSE(voice.processor.evolveOn());
        CHECK(voice.processor.generationStatus() == mm::plugin::GenerationStatus::UseHub);
    }
}

TEST_CASE("evolve: the settings are saved when they differ from the default, the switch is not saved",
          "[group-wiring][evolve]") {
    Quiet quiet;
    Instance source("Source");
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("evolveEvery"));
    CHECK_FALSE(root->hasAttribute("evolveStrength"));
    CHECK_FALSE(root->hasAttribute("evolveFromOriginal"));

    source.processor.setEvolveSettings({8, true, 55});
    source.processor.setEvolve(true);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const auto evolve = target.processor.instanceSettings().evolve;
    CHECK(evolve.everyPasses == 8);
    CHECK(evolve.fromOriginal);
    CHECK(evolve.strengthPct == 55);
    CHECK_FALSE(target.processor.evolveOn()); // the layer is not saved (SPEC 6.7)
    source.processor.setEvolve(false);

    // Values out of range are brought into range
    auto bad = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(bad != nullptr);
    bad->setAttribute("evolveEvery", 99);
    bad->setAttribute("evolveStrength", 0);
    juce::MemoryBlock badBlock;
    juce::AudioProcessor::copyXmlToBinary(*bad, badBlock);
    target.processor.setStateInformation(badBlock.getData(), static_cast<int>(badBlock.getSize()));
    CHECK(target.processor.instanceSettings().evolve.everyPasses == mm::core::kEvolveMaxPasses);
    CHECK(target.processor.instanceSettings().evolve.strengthPct == 1);
}

TEST_CASE("evolve: the controls are in the hub window, the settings only at the expert level, and nothing overlaps",
          "[group-wiring][editor][layout][evolve]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 13))); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    auto* toggle = child<juce::TextButton>(*editor, "evolve");
    auto* back = child<juce::TextButton>(*editor, "evolve_back");
    auto* keep = child<juce::TextButton>(*editor, "evolve_keep");
    auto* stage = child<juce::Label>(*editor, "evolve_stage");
    REQUIRE((toggle != nullptr && back != nullptr && keep != nullptr && stage != nullptr));
    CHECK(toggle->isVisible());
    CHECK(toggle->getButtonText() == "Evolve");
    CHECK(keep->getButtonText() == "Behalten");
    CHECK_FALSE(back->isEnabled());
    CHECK_FALSE(keep->isEnabled());
    CHECK(stage->getText() == "Stufe –");
    for (auto* component : std::initializer_list<juce::Component*>{toggle, back, keep, stage}) {
        CHECK(editor->getLocalBounds().contains(component->getBounds()));
        for (auto* other : editor->getChildren()) {
            if (other != component && other->isVisible() && other->getWidth() > 0 &&
                dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
                INFO(component->getComponentID().toStdString() << " vs " << other->getComponentID().toStdString());
                CHECK_FALSE(component->getBounds().intersects(other->getBounds()));
            }
        }
    }
    // the toggle switches the layer
    toggle->setToggleState(true, juce::dontSendNotification);
    toggle->onClick();
    CHECK(hub.processor.evolveOn());
    toggle->setToggleState(false, juce::dontSendNotification);
    toggle->onClick();
    CHECK_FALSE(hub.processor.evolveOn());

    // expert level: the row with the settings
    auto* every = child<juce::ComboBox>(*editor, "evolve_every");
    auto* original = child<juce::ToggleButton>(*editor, "evolve_original");
    auto* strength = child<juce::Slider>(*editor, "evolve_strength");
    REQUIRE((every != nullptr && original != nullptr && strength != nullptr));
    CHECK_FALSE(every->isVisible());
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(true, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK(every->isVisible());
    CHECK(original->isVisible());
    CHECK(strength->isVisible());
    CHECK(editor->getLocalBounds().contains(every->getBounds()));
    every->setSelectedId(6, juce::sendNotificationSync);
    original->setToggleState(true, juce::dontSendNotification);
    original->onClick();
    strength->setValue(70.0, juce::sendNotificationSync);
    const auto evolve = hub.processor.instanceSettings().evolve;
    CHECK(evolve.everyPasses == 6);
    CHECK(evolve.fromOriginal);
    CHECK(evolve.strengthPct == 70);
    // back to the basic level (the stored expert flag of the global settings is not touched by the other tests)
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
}

TEST_CASE("evolve: a stage starts exactly with a pass of the pattern, never in the middle", "[group-wiring][evolve]") {
    Quiet quiet;
    FakePlayheadClock clock;
    bool anyChange = false;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, seed))); });
        hub.processor.setEvolveSettings(evolveEvery(2, true, 100));
        hub.processor.setEvolveSeed(7);
        hub.processor.setEvolve(true);
        runEvolve(hub, 8.0 * 4 + 0.1);
        // The stage made at the start of pass 2 (8 PPQ long) plays from pass 3: pass 2, with both of its bars, is
        // still the slot as it was in pass 1.
        const auto pass = [&](int n) { return pitchesBetween(hub, 8.0 * n, 8.0 * (n + 1)); };
        INFO("seed " << seed);
        REQUIRE_FALSE(pass(0).empty());
        CHECK(pass(0) == pass(1));
        anyChange = anyChange || pass(2) != pass(1);
        hub.processor.setEvolve(false);
    }
    CHECK(anyChange);
}

namespace {

/// Steps all instances block by block (hub first) up to `ppq`; the hub's Evolve layer ticks after its block.
void runEvolveGroup(std::vector<Instance*> instances, double ppq) {
    while (instances.front()->ppq() < ppq - 1e-9) {
        for (auto* instance : instances) {
            instance->step();
        }
        instances.front()->processor.evolveTick();
    }
}

} // namespace

TEST_CASE("evolve in a group: the voices play the hub's stage and switch at the same position", "[group-wiring][evolve][group]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    Instance bass("Bass");
    Instance melody("Melody");
    hub.setRole(InstanceRole::Hub);
    bass.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    melody.setRole(InstanceRole::Voice, SlotFollow::Hub, 2);
    const mm::core::Pattern original = evolvePattern(hub, 21);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    hub.processor.setEvolveSettings(evolveEvery(2, false, 100));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolveGroup({&hub, &bass, &melody}, 8.0 * 8 + 0.1);

    const auto pass = [&](const Instance& instance, int n) { return pitchesBetween(instance, 8.0 * n, 8.0 * (n + 1)); };
    REQUIRE_FALSE(pass(bass, 1).empty());
    REQUIRE_FALSE(pass(melody, 1).empty());
    bool bassChanged = false;
    bool melodyChanged = false;
    for (int n = 0; n < 8; ++n) {
        INFO("pass " << n);
        // the hub plays the bass too (output voice 1): the bass voice sounds exactly as the hub, pass by pass
        CHECK(pass(bass, n) == pass(hub, n));
        bassChanged = bassChanged || pass(bass, n) != pass(bass, 0);
        melodyChanged = melodyChanged || pass(melody, n) != pass(melody, 0);
    }
    CHECK(bassChanged);
    CHECK(melodyChanged);
    // and the voices changed with the stages: pass 1 is stage 0 for all, the first change comes with pass 2 or later
    CHECK(pass(bass, 0) == pass(bass, 1));
    CHECK(pass(melody, 0) == pass(melody, 1));
    // the voices keep the slot set of the hub as it is: nothing of the stage is kept in them
    CHECK(sameNotes(*bass.processor.slotsSnapshot().slot(0)->pattern, original));
    CHECK(sameNotes(*melody.processor.slotsSnapshot().slot(0)->pattern, original));
}

TEST_CASE("evolve in a group: switching off brings every voice back to the slot at the same bar line",
          "[group-wiring][evolve][group]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    Instance bass("Bass");
    Instance melody("Melody");
    hub.setRole(InstanceRole::Hub);
    bass.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    melody.setRole(InstanceRole::Voice, SlotFollow::Hub, 2);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 22))); });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 100));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolveGroup({&hub, &bass, &melody}, 8.0 * 5 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);
    hub.processor.setEvolve(false);
    runEvolveGroup({&hub, &bass, &melody}, 8.0 * 9);
    const auto pass = [&](const Instance& instance, int n) { return pitchesBetween(instance, 8.0 * n, 8.0 * (n + 1)); };
    for (const Instance* instance : {&hub, &bass, &melody}) {
        INFO(instance->processor.getName().toStdString());
        // from the pass after the switch-off (pass 7 at the latest) the slot plays again, exactly as in the first pass
        CHECK(pass(*instance, 8) == pass(*instance, 0));
        CHECK(pass(*instance, 7) == pass(*instance, 0));
    }
}

TEST_CASE("evolve in a group: a voice that joins while a stage plays gets it", "[group-wiring][evolve][group]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    Instance bass("Bass");
    hub.setRole(InstanceRole::Hub);
    bass.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 23))); });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 100));
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    runEvolveGroup({&hub, &bass}, 8.0 * 5 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);
    Instance late("Late");
    late.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    late.setPpq(bass.ppq());
    runEvolveGroup({&hub, &bass, &late}, 8.0 * 9 + 0.1);
    const auto pass = [&](const Instance& instance, int n) { return pitchesBetween(instance, 8.0 * n, 8.0 * (n + 1)); };
    // The newcomer sounds like the hub and the other voice from the second full pass after it joined on
    CHECK(pass(late, 8) == pass(hub, 8));
    CHECK(pass(late, 8) == pass(bass, 8));
    CHECK(pass(late, 8) != pass(hub, 0));
}

TEST_CASE("evolve parameters: the host switches the layer, a trigger keeps the stage", "[group-wiring][evolve][parameters]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 24))); });
    hub.processor.setEvolveSettings(evolveEvery(1, false, 100));
    hub.processor.setEvolveSeed(7);
    auto* evolve = hub.processor.parameters().getParameter("evolve");
    auto* keep = hub.processor.parameters().getParameter("evolve_keep");
    REQUIRE((evolve != nullptr && keep != nullptr));
    CHECK(evolve->isAutomatable());
    CHECK(keep->isAutomatable());
    CHECK_FALSE(hub.processor.evolveOn());

    evolve->setValueNotifyingHost(1.0f);
    pumpMessages(20);
    CHECK(hub.processor.evolveOn());
    runEvolve(hub, 8.0 * 3 + 0.1);
    REQUIRE(hub.processor.evolveInfo().changed);

    keep->setValueNotifyingHost(1.0f); // the rising edge keeps the sounding stage
    pumpMessages(20);
    CHECK(hub.processor.generationStatus() == mm::plugin::GenerationStatus::EvolveKept);
    CHECK(hub.processor.slotsSnapshot().slot(0)->history.size() == 2);
    CHECK(keep->getValue() == 0.0f); // and it is a trigger again
    CHECK(hub.processor.evolveOn());

    evolve->setValueNotifyingHost(0.0f);
    pumpMessages(20);
    CHECK_FALSE(hub.processor.evolveOn());
    CHECK_FALSE(hub.processor.evolveInfo().layerActive);

    // the toggle of the window moves the parameter, and a switch-off through the object does too
    hub.processor.setEvolve(true);
    CHECK(evolve->getValue() == 1.0f);
    hub.processor.setEvolve(false);
    CHECK(evolve->getValue() == 0.0f);
}

TEST_CASE("evolve parameters: an offline render ignores them, a loaded state does not switch the layer on",
          "[group-wiring][evolve][parameters]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 25))); });
    hub.processor.setNonRealtime(true);
    hub.processor.parameters().getParameter("evolve")->setValueNotifyingHost(1.0f);
    pumpMessages(20);
    CHECK_FALSE(hub.processor.evolveOn());
    hub.processor.setNonRealtime(false);

    // a state saved with the parameter at 1 (the layer itself is not saved)
    Instance source("Source");
    source.processor.parameters().getParameter("evolve")->setValueNotifyingHost(1.0f);
    pumpMessages(20);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    source.processor.setEvolve(false);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    pumpMessages(40);
    CHECK_FALSE(target.processor.evolveOn());
}

TEST_CASE("evolve parameters: a voice cannot be switched on by the host either", "[group-wiring][evolve][parameters]") {
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    voice.processor.parameters().getParameter("evolve")->setValueNotifyingHost(1.0f);
    pumpMessages(20);
    CHECK_FALSE(voice.processor.evolveOn());
}

TEST_CASE("evolve in a group: the end is planned for the whole group, also for a voice that runs behind",
          "[group-wiring][evolve][group]") {
    Quiet quiet;
    FakePlayheadClock clock;
    Instance hub("Hub");
    Instance bass("Bass");
    hub.setRole(InstanceRole::Hub);
    bass.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 26))); });
    hub.processor.setEvolveSettings(evolveEvery(4, false, 100)); // no stage is waiting when the layer ends
    hub.processor.setEvolveSeed(7);
    hub.processor.setEvolve(true);
    hub.setPpq(0.5); // the hub's block runs half a PPQ ahead of the voice's: the voice reaches a bar line later
    runEvolveGroup({&hub, &bass}, 8.0 * 5 + 4.1); // stage 1 sounds since the pass at 32
    REQUIRE(hub.processor.evolveInfo().changed);
    // The hub is just behind the bar line at 44, the voice is not there yet. Without a plan the voice would go back to
    // the slot at its own bar line 44 and the hub at 48; planned, both go at 48 on their own clock.
    hub.processor.setEvolve(false);
    runEvolveGroup({&hub, &bass}, 8.0 * 8 + 0.1);
    // Between 44 and 48 on their own clocks both still play the stage, and alike
    CHECK(pitchesBetween(bass, 44.0, 48.0) == pitchesBetween(hub, 44.0, 48.0));
    // From 48 on both play the slot again
    CHECK(pitchesBetween(bass, 56.0, 60.0) == pitchesBetween(hub, 56.0, 60.0));
    CHECK(pitchesBetween(bass, 48.0, 52.0) == pitchesBetween(hub, 48.0, 52.0));
    // the stage in 44..48 is not the slot (else the checks above would prove nothing)
    CHECK(pitchesBetween(hub, 44.0, 48.0) != pitchesBetween(hub, 52.0, 56.0));
}

TEST_CASE("evolve in a group: a voice that runs behind the hub still plays every stage, also when each pass has a new one",
          "[group-wiring][evolve][group]") {
    Quiet quiet;
    FakePlayheadClock clock;
    for (const double lag : {0.1, 0.5, 2.0}) {
        Instance hub("Hub");
        Instance bass("Bass");
        hub.setRole(InstanceRole::Hub);
        bass.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 26))); });
        hub.processor.setEvolveSettings(evolveEvery(1, false, 100));
        hub.processor.setEvolveSeed(7);
        hub.processor.setEvolve(true);
        hub.setPpq(lag); // the hub's block runs `lag` PPQ ahead of the voice's
        runEvolveGroup({&hub, &bass}, 8.0 * 8 + 0.1);
        // Both play the same pattern on their own clock, pass by pass; the voice neither skips nor repeats a stage
        const auto pass = [&](const Instance& instance, int n) { return pitchesBetween(instance, 8.0 * n, 8.0 * (n + 1)); };
        INFO("lag " << lag);
        for (int n = 2; n < 7; ++n) { // the voice has played up to 8 * 8 + 0.1 - lag
            INFO("pass " << n);
            CHECK(pass(bass, n) == pass(hub, n));
        }
        CHECK(pass(bass, 3) != pass(bass, 4));
        hub.processor.setEvolve(false);
    }
}

// --- live transposition (SPEC 3.16) ---------------------------------------------------------------------------

namespace {

/// A one-bar bass with a note of `pitch` on each of the four beats (key A minor, reference note 45).
mm::core::Pattern beatPattern(uint8_t pitch, uint8_t second = 0) {
    mm::core::Pattern pattern = mm::core::makeEmptyPattern(1, "peak_time");
    for (uint32_t beat = 0; beat < 4; ++beat) {
        mm::core::Note note;
        note.id = mm::core::allocateNoteId(pattern);
        note.pitch = pitch;
        note.startTick = beat * 960 + 120; // an eighth after the beat: never at the edge of a block
        note.lengthTicks = 240;
        pattern.voices[0].notes.push_back(note);
    }
    if (second != 0) { // a melody note of its own
        mm::core::Note note;
        note.id = mm::core::allocateNoteId(pattern);
        note.pitch = second;
        note.startTick = 120;
        note.lengthTicks = 240;
        pattern.voices[1].notes.push_back(note);
    }
    return pattern;
}

struct TransposeRig {
    Quiet quiet;
    Instance solo{"Solo"};
    explicit TransposeRig(uint8_t pitch = 41, uint8_t second = 0, int voice = 1) {
        solo.setRole(InstanceRole::Solo, SlotFollow::Hub, voice);
        solo.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, beatPattern(pitch, second))); });
        solo.setSlotParameter(1);
        // The pattern is playing before anything is pressed: the key of the playing pattern is the reference's.
        while (solo.ppq() < 0.5) {
            solo.step();
        }
    }
    void run(double ppq) {
        while (solo.ppq() < ppq - 1e-9) {
            solo.step();
        }
    }
    /// The pitches that started between two PPQ positions.
    std::vector<int> pitches(double from, double to) const {
        std::vector<int> result;
        for (const auto& note : solo.notes()) {
            if (note.sample + 2 >= at(from) && note.sample + 2 < at(to)) { // a note at a bar line may be a sample early
                result.push_back(note.pitch);
            }
        }
        return result;
    }
    void setSettings(const std::function<void(mm::core::TransposeSettings&)>& change) {
        auto settings = solo.processor.transposeSettings();
        change(settings);
        solo.processor.setTransposeSettings(settings);
    }
};

} // namespace

TEST_CASE("transpose: the settings are packed and unpacked without loss", "[group-wiring][transpose]") {
    Quiet quiet;
    Instance instance("Solo");
    CHECK(instance.processor.transposeSettings() == mm::core::TransposeSettings{});
    for (const auto mode : {mm::core::TransposeMode::Chromatic, mm::core::TransposeMode::Diatonic}) {
        for (const auto behavior : {mm::core::TransposeBehavior::Hold, mm::core::TransposeBehavior::Momentary}) {
            for (const auto timing : {mm::core::TransposeTiming::NextNote, mm::core::TransposeTiming::NextBeat,
                                      mm::core::TransposeTiming::NextBar}) {
                for (const auto target : {mm::core::TransposeTarget::Both, mm::core::TransposeTarget::Bass,
                                          mm::core::TransposeTarget::Melody}) {
                    for (const int channel : {0, 1, 9, 16}) {
                        for (const int reference : {-1, 0, 45, 127}) {
                            const mm::core::TransposeSettings settings{mode, behavior, timing, target, reference, channel};
                            instance.processor.setTransposeSettings(settings);
                            CHECK(instance.processor.transposeSettings() == settings);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("transpose: a key transposes by its distance to the reference, from the next note on, and holds", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.run(4.0); // the bar plays 41 on every beat
    CHECK(rig.pitches(0.0, 4.0) == std::vector<int>{41, 41, 41, 41});
    CHECK(rig.solo.processor.effectiveTranspose() == 0);
    rig.solo.keyDown(48); // reference is A2 = 45: +3
    rig.solo.step();
    CHECK(rig.solo.processor.keyboardTranspose() == 3);
    CHECK(rig.solo.processor.effectiveTranspose() == 3);
    rig.run(8.0);
    CHECK(rig.pitches(4.0, 8.0) == std::vector<int>{44, 44, 44, 44});
    rig.solo.keyUp(48); // hold: the key up changes nothing
    rig.run(12.0);
    CHECK(rig.pitches(8.0, 12.0) == std::vector<int>{44, 44, 44, 44});
    rig.solo.keyDown(43); // -2 from the reference
    rig.run(16.0);
    CHECK(rig.pitches(12.0, 16.0) == std::vector<int>{39, 39, 39, 39});
    rig.solo.keyDown(45); // the reference itself is back to 0
    rig.run(20.0);
    CHECK(rig.pitches(16.0, 20.0) == std::vector<int>{41, 41, 41, 41});
    // and nothing of the keyboard is passed on: no note of the pitches that were pressed
    for (const auto& note : rig.solo.notes()) {
        CHECK(note.pitch != 48);
        CHECK(note.pitch != 43);
        CHECK(note.pitch != 45);
    }
}

TEST_CASE("transpose: momentary lasts as long as the key", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.setSettings([](auto& s) { s.behavior = mm::core::TransposeBehavior::Momentary; });
    rig.run(1.0);
    rig.solo.keyDown(50);
    rig.solo.keyDown(52); // a second key: the last one counts
    rig.run(4.0);
    CHECK(rig.pitches(1.0, 4.0) == std::vector<int>{48, 48, 48});
    rig.solo.keyUp(50); // not the key that set it: nothing changes
    rig.run(6.0);
    CHECK(rig.pitches(4.0, 6.0) == std::vector<int>{48, 48});
    rig.solo.keyUp(52);
    rig.run(8.0);
    CHECK(rig.pitches(6.0, 8.0) == std::vector<int>{41, 41});
}

TEST_CASE("transpose: the timing waits for the next beat or the next bar, sample-accurate", "[group-wiring][transpose]") {
    for (const auto timing : {mm::core::TransposeTiming::NextBeat, mm::core::TransposeTiming::NextBar}) {
        TransposeRig rig;
        rig.setSettings([&](auto& s) { s.timing = timing; });
        rig.run(1.3);
        rig.solo.keyDown(47); // +2, in the middle of the second beat
        rig.run(8.0);
        INFO("timing " << static_cast<int>(timing));
        if (timing == mm::core::TransposeTiming::NextBeat) {
            CHECK(rig.pitches(0.0, 4.0) == std::vector<int>{41, 41, 43, 43}); // from beat 3 (2.0) on
        } else {
            CHECK(rig.pitches(0.0, 4.0) == std::vector<int>{41, 41, 41, 41});
            CHECK(rig.pitches(4.0, 8.0) == std::vector<int>{43, 43, 43, 43}); // from the bar line
        }
        // the shown value is the one that is in force: after the point
        CHECK(rig.solo.processor.keyboardTranspose() == 2);
    }
}

TEST_CASE("transpose: with the transport stopped a key is in force at once", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.setSettings([](auto& s) { s.timing = mm::core::TransposeTiming::NextBar; });
    rig.solo.setPlaying(false);
    rig.solo.step();
    rig.solo.keyDown(48);
    rig.solo.step();
    CHECK(rig.solo.processor.keyboardTranspose() == 3);
    rig.solo.setPlaying(true);
    rig.run(4.0);
    CHECK(rig.pitches(1.0, 4.0) == std::vector<int>{44, 44, 44}); // the first bar already
}

TEST_CASE("transpose: the host parameter and the keyboard add up and are limited", "[group-wiring][transpose]") {
    TransposeRig rig;
    auto* parameter = rig.solo.processor.parameters().getParameter("transpose");
    REQUIRE(parameter != nullptr);
    CHECK(parameter->isAutomatable());
    parameter->setValueNotifyingHost(parameter->convertTo0to1(2.0f));
    rig.run(4.0);
    CHECK(rig.pitches(1.0, 4.0) == std::vector<int>{43, 43, 43}); // the parameter alone
    CHECK(rig.solo.processor.effectiveTranspose() == 2);
    CHECK(rig.solo.processor.keyboardTranspose() == 0);
    rig.solo.keyDown(48); // +3
    rig.run(8.0);
    CHECK(rig.pitches(4.0, 8.0) == std::vector<int>{46, 46, 46, 46}); // 2 + 3
    CHECK(rig.solo.processor.effectiveTranspose() == 5);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(12.0f));
    rig.run(12.0);
    CHECK(rig.solo.processor.effectiveTranspose() == 12); // 12 + 3 limited
    // the keyboard offset is not written back into the parameter
    CHECK(static_cast<int>(std::lround(parameter->convertFrom0to1(parameter->getValue()))) == 12);
}

TEST_CASE("transpose: the target picks the voice", "[group-wiring][transpose]") {
    for (const auto target : {mm::core::TransposeTarget::Bass, mm::core::TransposeTarget::Melody,
                              mm::core::TransposeTarget::Both}) {
        for (const int voice : {1, 2}) {
            TransposeRig rig(41, 60, voice);
            rig.setSettings([&](auto& s) { s.target = target; });
            rig.solo.keyDown(48);
            rig.run(4.0);
            const bool hit = target == mm::core::TransposeTarget::Both ||
                             (target == mm::core::TransposeTarget::Bass) == (voice == 1);
            INFO("target " << static_cast<int>(target) << " voice " << voice);
            const int base = voice == 1 ? 41 : 60;
            if (voice == 1) {
                CHECK(rig.pitches(1.0, 4.0) == std::vector<int>(3, hit ? base + 3 : base));
            }
            CHECK(rig.solo.processor.effectiveTranspose() == (hit ? 3 : 0));
        }
    }
}

TEST_CASE("transpose: only the chosen MIDI channel counts", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.setSettings([](auto& s) { s.channel = 3; });
    rig.solo.keyDown(48, 1);
    rig.run(4.0);
    CHECK(rig.solo.processor.keyboardTranspose() == 0);
    rig.solo.keyDown(48, 3);
    rig.run(8.0);
    CHECK(rig.solo.processor.keyboardTranspose() == 3);
    CHECK(rig.pitches(4.0, 8.0) == std::vector<int>{44, 44, 44, 44});
}

TEST_CASE("transpose: the reference note can be set", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.setSettings([](auto& s) { s.referenceNote = 60; });
    rig.solo.keyDown(62); // +2 from C4
    rig.run(4.0);
    CHECK(rig.solo.processor.keyboardTranspose() == 2);
    CHECK(rig.pitches(1.0, 4.0) == std::vector<int>{43, 43, 43});
}

TEST_CASE("transpose: the diatonic mode moves by degrees and keeps the key", "[group-wiring][transpose]") {
    TransposeRig rig(40); // E in A minor
    rig.setSettings([](auto& s) { s.mode = mm::core::TransposeMode::Diatonic; });
    rig.solo.keyDown(48); // A2 -> C3: two degrees up
    rig.run(4.0);
    CHECK(rig.solo.processor.keyboardTranspose() == 2);
    CHECK(rig.pitches(1.0, 4.0) == std::vector<int>{43, 43, 43}); // E -> G
}

TEST_CASE("transpose: an offline render ignores the keyboard, a voice does not take part", "[group-wiring][transpose]") {
    {
        TransposeRig rig;
        rig.solo.keyDown(48);
        rig.run(4.0);
        REQUIRE(rig.solo.processor.keyboardTranspose() == 3);
        rig.solo.processor.setNonRealtime(true);
        rig.solo.keyDown(50);
        rig.run(8.0);
        CHECK(rig.solo.processor.keyboardTranspose() == 0); // nothing of the keyboard stays in a bounce
        CHECK(rig.pitches(4.0, 8.0) == std::vector<int>{41, 41, 41, 41});
        rig.solo.processor.setNonRealtime(false);
    }
    {
        TransposeRig rig;
        rig.solo.setRole(InstanceRole::Voice);
        rig.solo.keyDown(48);
        rig.run(4.0);
        CHECK(rig.solo.processor.keyboardTranspose() == 0);
        CHECK(rig.solo.processor.effectiveTranspose() == 0);
    }
}

TEST_CASE("transpose: a transposition during sounding notes leaves no hanging note", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.run(0.1); // the first note sounds
    rig.solo.keyDown(52);
    rig.solo.keyUp(52);
    rig.solo.keyDown(46);
    rig.run(8.0);
    // every note-on has its note-off, on the pitch that was sent
    std::map<int, int> balance;
    for (const auto& note : rig.solo.notes()) {
        ++balance[note.pitch];
    }
    for (const auto& off : rig.solo.noteOffs()) {
        --balance[off.pitch];
    }
    for (const auto& [pitch, count] : balance) {
        INFO("pitch " << pitch);
        CHECK(count == 0);
    }
}

TEST_CASE("transpose: the hub window shows the live transposition next to the slot", "[group-wiring][transpose][editor]") {
    TransposeRig rig;
    rig.solo.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.solo.processor.createEditor());
    pumpMessages(60);
    auto* info = child<juce::Label>(*editor, "info");
    REQUIRE(info != nullptr);
    CHECK_FALSE(info->getText().contains("Transponiert"));
    rig.solo.keyDown(48);
    rig.run(2.0);
    CHECK(waitFor([&] { return info->getText().contains("Transponiert +3"); }));
    rig.solo.keyDown(43);
    rig.run(3.0);
    CHECK(waitFor([&] { return info->getText().contains("Transponiert -2"); }));
    rig.solo.keyDown(45);
    rig.run(4.0);
    CHECK(waitFor([&] { return !info->getText().contains("Transponiert"); }));
}

TEST_CASE("transpose: a change that waits for its bar line takes effect when the transport stops", "[group-wiring][transpose]") {
    TransposeRig rig;
    rig.setSettings([](auto& s) { s.timing = mm::core::TransposeTiming::NextBar; });
    rig.run(1.3);
    rig.solo.keyDown(48);
    rig.solo.step();
    CHECK(rig.solo.processor.keyboardTranspose() == 0); // it waits for the bar line at 4
    rig.solo.setPlaying(false);
    rig.solo.step();
    CHECK(rig.solo.processor.keyboardTranspose() == 3); // nothing is waiting for a stopped transport
}

// --- live transposition in a group (SPEC 3.16, D-216) ---------------------------------------------------------

namespace {

/// Hub (plays voice 1), a bass voice and a melody voice that follow it; the keyboard goes to the hub.
struct TransposeGroup {
    Quiet quiet;
    Instance hub{"Hub"};
    Instance bass{"Bass"};
    Instance melody{"Melody"};
    TransposeGroup() {
        hub.setRole(InstanceRole::Hub);
        bass.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
        melody.setRole(InstanceRole::Voice, SlotFollow::Hub, 2);
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, beatPattern(41, 60))); });
        hub.setSlotParameter(1);
        run(0.5);
    }
    void run(double ppq) { runTo({&hub, &bass, &melody}, ppq); }
    void setAll(const std::function<void(mm::core::TransposeSettings&)>& change) {
        for (auto* instance : {&hub, &bass, &melody}) {
            auto settings = instance->processor.transposeSettings();
            change(settings);
            instance->processor.setTransposeSettings(settings);
        }
    }
    static std::vector<int> pitches(const Instance& instance, double from, double to) {
        std::vector<int> result;
        for (const auto& note : instance.notes()) {
            if (note.sample + 2 >= at(from) && note.sample + 2 < at(to)) {
                result.push_back(note.pitch);
            }
        }
        return result;
    }
    /// The sample of the first note-on of `pitch`, or -1.
    static long long first(const Instance& instance, int pitch) { return instance.firstWith(pitch); }
};

} // namespace

TEST_CASE("transpose in a group: the hub plans the change, hub and voices transpose at the same beat", "[group-wiring][transpose][group]") {
    TransposeGroup group;
    group.run(1.3);
    group.hub.keyDown(48); // +3, the hub's block is at 1.3: with the lead the next beat that can be kept is 2
    group.run(8.0);
    // bass: notes on the beats +0.125: those from 2.125 on are shifted, nothing before
    for (const Instance* instance : {&group.hub, &group.bass}) {
        INFO(instance->processor.getName().toStdString());
        CHECK(TransposeGroup::pitches(*instance, 1.0, 2.0) == std::vector<int>{41});
        CHECK(TransposeGroup::pitches(*instance, 2.0, 4.0) == std::vector<int>{44, 44});
        CHECK(TransposeGroup::pitches(*instance, 4.0, 8.0) == std::vector<int>{44, 44, 44, 44});
    }
    // the same sample for hub and voice
    CHECK(near(TransposeGroup::first(group.hub, 44), TransposeGroup::first(group.bass, 44)));
    // the melody voice has one note a bar: 60 in bar 1, 63 afterwards
    CHECK(TransposeGroup::pitches(group.melody, 0.0, 4.0) == std::vector<int>{60}); // the first one is before the key
    CHECK(TransposeGroup::pitches(group.melody, 4.0, 8.0) == std::vector<int>{63});
    // what the windows show
    CHECK(group.hub.processor.effectiveTranspose() == 3);
    CHECK(group.bass.processor.effectiveTranspose() == 3);
    CHECK(group.melody.processor.effectiveTranspose() == 3);
    CHECK(group.bass.processor.keyboardTranspose() == 0); // a voice has no keyboard of its own
}

TEST_CASE("transpose in a group: the bar timing plans a bar line", "[group-wiring][transpose][group]") {
    TransposeGroup group;
    group.setAll([](auto& s) { s.timing = mm::core::TransposeTiming::NextBar; });
    group.run(1.3);
    group.hub.keyDown(50); // +5
    group.run(8.0);
    for (const Instance* instance : {&group.hub, &group.bass}) {
        CHECK(TransposeGroup::pitches(*instance, 1.0, 4.0) == std::vector<int>{41, 41, 41});
        CHECK(TransposeGroup::pitches(*instance, 4.0, 8.0) == std::vector<int>{46, 46, 46, 46});
    }
    CHECK(TransposeGroup::pitches(group.melody, 4.0, 8.0) == std::vector<int>{65});
}

TEST_CASE("transpose in a group: the target decides which voices move", "[group-wiring][transpose][group]") {
    TransposeGroup group;
    group.setAll([](auto& s) { s.target = mm::core::TransposeTarget::Melody; });
    group.run(1.3);
    group.hub.keyDown(48);
    group.run(8.0);
    CHECK(TransposeGroup::pitches(group.hub, 4.0, 8.0) == std::vector<int>{41, 41, 41, 41}); // the hub plays the bass
    CHECK(TransposeGroup::pitches(group.bass, 4.0, 8.0) == std::vector<int>{41, 41, 41, 41});
    CHECK(TransposeGroup::pitches(group.melody, 4.0, 8.0) == std::vector<int>{63});
    CHECK(group.hub.processor.effectiveTranspose() == 0);
    CHECK(group.melody.processor.effectiveTranspose() == 3);
    CHECK(group.hub.processor.transposeTotal() == 3); // the group's total is what the hub knows
}

TEST_CASE("transpose in a group: the host parameter of the hub is planned for the group, the voices' own is ignored",
          "[group-wiring][transpose][group]") {
    TransposeGroup group;
    auto* voiceParameter = group.bass.processor.parameters().getParameter("transpose");
    voiceParameter->setValueNotifyingHost(voiceParameter->convertTo0to1(7.0f)); // a voice follows the hub
    group.run(1.3);
    auto* hubParameter = group.hub.processor.parameters().getParameter("transpose");
    hubParameter->setValueNotifyingHost(hubParameter->convertTo0to1(2.0f));
    group.run(8.0);
    for (const Instance* instance : {&group.hub, &group.bass}) {
        INFO(instance->processor.getName().toStdString());
        CHECK(TransposeGroup::pitches(*instance, 1.0, 2.0) == std::vector<int>{41});
        CHECK(TransposeGroup::pitches(*instance, 4.0, 8.0) == std::vector<int>{43, 43, 43, 43});
    }
    CHECK(near(TransposeGroup::first(group.hub, 43), TransposeGroup::first(group.bass, 43)));
    CHECK(group.bass.processor.effectiveTranspose() == 2);
}

TEST_CASE("transpose in a group: a voice set to own plays its own parameter", "[group-wiring][transpose][group]") {
    TransposeGroup group;
    group.bass.setRole(InstanceRole::Voice, SlotFollow::Own, 1);
    auto* parameter = group.bass.processor.parameters().getParameter("transpose");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(4.0f));
    group.run(1.3);
    group.hub.keyDown(48);
    group.run(8.0);
    CHECK(TransposeGroup::pitches(group.bass, 4.0, 8.0) == std::vector<int>{45, 45, 45, 45}); // 41 + its own 4, not the hub's 3
    CHECK(TransposeGroup::pitches(group.hub, 4.0, 8.0) == std::vector<int>{44, 44, 44, 44});
    CHECK(group.bass.processor.effectiveTranspose() == 4);
}

TEST_CASE("transpose in a group: a voice that joins takes what is in force, momentary releases the whole group",
          "[group-wiring][transpose][group]") {
    TransposeGroup group;
    group.setAll([](auto& s) { s.behavior = mm::core::TransposeBehavior::Momentary; });
    group.run(1.3);
    group.hub.keyDown(48);
    group.run(5.0);
    CHECK(group.hub.processor.transposeTotal() == 3);
    Instance late("Late");
    late.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    {
        auto settings = late.processor.transposeSettings();
        settings.behavior = mm::core::TransposeBehavior::Momentary;
        late.processor.setTransposeSettings(settings);
    }
    late.setPpq(group.hub.ppq());
    runTo({&group.hub, &group.bass, &group.melody, &late}, 9.0);
    for (const double from : {6.0, 7.0, 8.0}) {
        CHECK(TransposeGroup::pitches(late, from, from + 1.0) == std::vector<int>{44}); // from the first note on
    }
    CHECK(late.processor.effectiveTranspose() == 3);
    group.hub.keyUp(48);
    runTo({&group.hub, &group.bass, &group.melody, &late}, 14.0);
    for (const Instance* instance : {&group.hub, &group.bass, &late}) {
        INFO(instance->processor.getName().toStdString());
        CHECK(instance->processor.effectiveTranspose() == 0);
        CHECK(TransposeGroup::pitches(*instance, 12.0, 14.0) == std::vector<int>{41, 41});
    }
}

TEST_CASE("transpose in a group: a voice that runs behind the hub transposes at the same position on its own clock",
          "[group-wiring][transpose][group]") {
    for (const double lag : {0.1, 0.5}) {
        TransposeGroup group;
        group.hub.setPpq(group.hub.ppq() + lag); // the hub's block runs ahead of the voices'
        group.run(group.hub.ppq() + 0.8);
        group.hub.keyDown(48);
        group.run(10.0);
        INFO("lag " << lag);
        // Each on its own clock: the notes of a beat window carry the same pitches in hub and voice
        for (double from = 2.0; from < 8.0; from += 1.0) {
            CHECK(TransposeGroup::pitches(group.bass, from, from + 1.0) == TransposeGroup::pitches(group.hub, from, from + 1.0));
        }
        CHECK(group.bass.processor.effectiveTranspose() == 3);
    }
}

TEST_CASE("transpose in a group: an offline render of the hub ignores the keyboard", "[group-wiring][transpose][group]") {
    TransposeGroup group;
    group.run(1.3);
    group.hub.keyDown(48);
    group.run(5.0);
    REQUIRE(group.bass.processor.effectiveTranspose() == 3);
    group.hub.processor.setNonRealtime(true); // the hub's keyboard is off in a bounce: the total falls to the parameter
    group.hub.keyDown(50);
    group.run(8.0);
    CHECK(group.hub.processor.keyboardTranspose() == 0);
    group.hub.processor.setNonRealtime(false);
}

TEST_CASE("transpose in a group: a voice drags out its notes with the transposition of the group", "[group-wiring][transpose][group][export]") {
    TransposeGroup group;
    group.run(1.3);
    group.hub.keyDown(48);
    group.run(6.0);
    REQUIRE(group.bass.processor.transposeTotal() == 3);
    group.bass.processor.updateExport();
    const auto pitchesOf = [](const juce::File& file) {
        juce::MidiFile midi;
        juce::FileInputStream stream(file);
        REQUIRE(stream.openedOk());
        REQUIRE(midi.readFrom(stream));
        std::vector<int> pitches;
        for (const auto* event : *midi.getTrack(0)) {
            if (event->message.isNoteOn()) {
                pitches.push_back(event->message.getNoteNumber());
            }
        }
        return pitches;
    };
    const auto& exporter = group.bass.processor.midiExporter();
    REQUIRE(waitFor([&] { return exporter.readyFile().existsAsFile() && pitchesOf(exporter.readyFile()).size() >= 3; }));
    const auto pitches = pitchesOf(exporter.readyFile());
    CHECK(pitches == std::vector<int>(pitches.size(), 44)); // 41 + 3
    // the melody voice (target both): 60 + 3
    group.melody.processor.updateExport();
    const auto& melodyExporter = group.melody.processor.midiExporter();
    REQUIRE(waitFor([&] { return melodyExporter.readyFile().existsAsFile() && !pitchesOf(melodyExporter.readyFile()).empty(); }));
    CHECK(pitchesOf(melodyExporter.readyFile()).front() == 63);
}

// --- transposition settings from the settings file, MIDI Thru (SPEC 8.4, 3.17, D-217) -----------------------------

TEST_CASE("the settings store counts its changes", "[settings][store][transpose]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    const auto start = store.revision();
    CHECK(start > 0); // the read of the file counts
    store.update([](mm::core::GlobalSettings& settings) { settings.expert = true; });
    CHECK(store.revision() == start + 1);
    store.update([](mm::core::GlobalSettings& settings) { settings.expert = true; }); // nothing changed
    CHECK(store.revision() == start + 1);
    store.update([](mm::core::GlobalSettings& settings) { settings.transpose.midiThru = true; });
    CHECK(store.revision() == start + 2);
    store.waitForWrites();
    // the transposition is in the file and a new store reads it
    mm::plugin::GlobalSettingsStore other(temp.file);
    REQUIRE(waitFor([&] { return other.ready(); }));
    CHECK(other.get().transpose.midiThru);
}

TEST_CASE("an instance takes the transposition settings from the settings file, once per change", "[group-wiring][transpose][settings]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Instance instance("Solo");
    CHECK(instance.processor.transposeSettings() == mm::core::TransposeSettings{});
    mm::core::TransposeSettings wanted{mm::core::TransposeMode::Diatonic,   mm::core::TransposeBehavior::Momentary,
                                       mm::core::TransposeTiming::NextBar, mm::core::TransposeTarget::Bass,
                                       50,                                 4,
                                       false,                              true};
    store.update([&](mm::core::GlobalSettings& settings) { settings.transpose = wanted; });
    instance.processor.syncTransposeSettings();
    CHECK(instance.processor.transposeSettings() == wanted);
    // nothing changed in the file: what is set directly stays
    auto direct = wanted;
    direct.channel = 9;
    instance.processor.setTransposeSettings(direct);
    instance.processor.syncTransposeSettings();
    CHECK(instance.processor.transposeSettings() == direct);
    // the next change in the file wins again
    store.update([&](mm::core::GlobalSettings& settings) { settings.transpose.channel = 2; });
    instance.processor.syncTransposeSettings();
    CHECK(instance.processor.transposeSettings().channel == 2);
}

TEST_CASE("the timer of an instance brings in a change of the settings file by itself", "[group-wiring][transpose][settings]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Instance instance("Solo");
    store.update([](mm::core::GlobalSettings& settings) { settings.transpose.timing = mm::core::TransposeTiming::NextBeat; });
    CHECK(waitFor([&] { return instance.processor.transposeSettings().timing == mm::core::TransposeTiming::NextBeat; }));
    // an instance that is created later starts with what the file says
    Instance later("Later");
    CHECK(later.processor.transposeSettings().timing == mm::core::TransposeTiming::NextBeat);
}

TEST_CASE("a store without file leaves the settings that were set directly", "[group-wiring][transpose][settings]") {
    Quiet quiet; // the tests of the other parts run with a store without file (NoUserSettings.h)
    Instance instance("Solo");
    mm::core::TransposeSettings direct;
    direct.target = mm::core::TransposeTarget::Melody;
    instance.processor.setTransposeSettings(direct);
    instance.processor.syncTransposeSettings();
    pumpMessages(300);
    CHECK(instance.processor.transposeSettings() == direct);
}

TEST_CASE("MIDI Thru passes the incoming MIDI on, off by default", "[group-wiring][transpose][thru]") {
    for (const bool thru : {false, true}) {
        TransposeRig rig;
        rig.setSettings([&](auto& s) { s.midiThru = thru; });
        rig.run(1.0);
        rig.solo.keyDown(100); // a note the pattern never plays
        rig.solo.step();
        rig.solo.keyUp(100);
        rig.solo.step();
        INFO("thru " << thru);
        CHECK(rig.solo.played(100) == thru);
        bool off = false;
        for (const auto& note : rig.solo.noteOffs()) {
            off = off || note.pitch == 100;
        }
        CHECK(off == thru);
        // the transposition works either way: the key is 100 - 45 = 55, limited to 12
        CHECK(rig.solo.processor.keyboardTranspose() == 12);
    }
}

TEST_CASE("a transposition setting changed before the file is read wins and the rest of the file stays", "[settings][store][transpose]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    temp.writeFile(R"({"version": 1, "variationStrength": 44, "transpose": {"mode": "diatonic", "channel": 5}})");
    auto& store = temp.open(false);
    store.update([](mm::core::GlobalSettings& settings) { settings.transpose.midiThru = true; });
    store.beginRead();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.waitForWrites();
    const auto now = store.get();
    CHECK(now.transpose.midiThru); // the change made first
    CHECK(now.transpose.mode == mm::core::TransposeMode::Diatonic); // what the file said stays, field by field
    CHECK(now.transpose.channel == 5);
    CHECK(now.variationStrength == 44);
    CHECK(mm::core::parseGlobalSettings(temp.file.loadFileAsString().toStdString()) == now);
}

// --- trigger parameters (SPEC 3.13, D-218) --------------------------------------------------------------------

namespace {

void pulse(Instance& instance, const char* id) {
    auto* parameter = instance.processor.parameters().getParameter(id);
    REQUIRE(parameter != nullptr);
    parameter->setValueNotifyingHost(1.0f); // the rising edge
    pumpMessages(30);
}

float valueOf(Instance& instance, const char* id) {
    return instance.processor.parameters().getParameter(id)->getValue();
}

size_t historySize(Instance& instance) {
    return instance.processor.slotsSnapshot().slot(0)->history.size();
}

} // namespace

TEST_CASE("trigger parameters: variation n varies voice n, the trigger falls back to 0", "[group-wiring][trigger]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    const mm::core::Pattern original = evolvePattern(hub, 31);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    for (const size_t voice : {size_t{0}, size_t{1}}) {
        Instance fresh("Hub");
        fresh.setRole(InstanceRole::Hub);
        fresh.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
        pulse(fresh, voice == 0 ? "variation_1" : "variation_2");
        INFO("voice " << voice + 1);
        const auto bank = fresh.processor.slotsSnapshot();
        CHECK(historySize(fresh) == 2); // the variation is a result
        CHECK(fresh.processor.generationStatus() == mm::plugin::GenerationStatus::Varied);
        const auto& now = *bank.slot(0)->pattern;
        CHECK(now.voices[voice].notes != original.voices[voice].notes);
        CHECK(now.voices[1 - voice].notes == original.voices[1 - voice].notes); // the other voice stays
        CHECK(valueOf(fresh, voice == 0 ? "variation_1" : "variation_2") == 0.0f); // a trigger: ready for the next edge
        pulse(fresh, voice == 0 ? "variation_1" : "variation_2");
        CHECK(historySize(fresh) == 3); // and it fires again
    }
}

TEST_CASE("trigger parameters: variation all varies every voice, a number without a voice does nothing",
          "[group-wiring][trigger]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    const mm::core::Pattern original = evolvePattern(hub, 32);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    pulse(hub, "variation_5"); // the pattern has two voices
    CHECK(historySize(hub) == 1);
    CHECK(hub.processor.generationStatus() == mm::plugin::GenerationStatus::NothingToVary);
    pulse(hub, "variation_all");
    CHECK(historySize(hub) == 2);
    const auto& now = *hub.processor.slotsSnapshot().slot(0)->pattern;
    CHECK(now.voices[0].notes != original.voices[0].notes);
    CHECK(now.voices[1].notes != original.voices[1].notes);
}

TEST_CASE("trigger parameters: the strength is the one of the settings", "[group-wiring][trigger]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) { settings.variationStrength = 100; });
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 33))); });
    pulse(hub, "variation_all");
    const size_t strong = hub.processor.lastVariationChanges();
    store.update([](mm::core::GlobalSettings& settings) { settings.variationStrength = 5; });
    Instance other("Hub2");
    other.setRole(InstanceRole::Hub); // a second hub is refused: it is solo then, which varies all the same
    other.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(other, 33))); });
    pulse(other, "variation_all");
    CHECK(other.processor.lastVariationChanges() < strong);
}

TEST_CASE("trigger parameters: a falling edge and a bounce do nothing", "[group-wiring][trigger]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 34))); });
    auto* parameter = hub.processor.parameters().getParameter("variation_1");
    parameter->setValueNotifyingHost(0.0f); // already 0
    pumpMessages(30);
    CHECK(historySize(hub) == 1);
    hub.processor.setNonRealtime(true);
    pulse(hub, "variation_1");
    pulse(hub, "variation_all");
    pulse(hub, "generate");
    CHECK(historySize(hub) == 1); // SPEC 3.13: not in an offline render
    CHECK(hub.processor.generationStatus() != mm::plugin::GenerationStatus::Generating); // no request was started
    hub.processor.setNonRealtime(false);
}

TEST_CASE("trigger parameters: generate fills the slot like the button", "[group-wiring][trigger]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    CHECK(hub.processor.slotsSnapshot().isEmpty(0));
    pulse(hub, "generate");
    CHECK(waitFor([&] { return !hub.processor.slotsSnapshot().isEmpty(0); }));
    CHECK(valueOf(hub, "generate") == 0.0f);
}

TEST_CASE("trigger parameters: a voice's triggers go to the hub", "[group-wiring][trigger][group]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    const mm::core::Pattern original = evolvePattern(hub, 35);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, original)); });
    pulse(voice, "variation_2"); // the hub varies voice 2
    CHECK(voice.processor.generationStatus() == mm::plugin::GenerationStatus::Forwarded);
    CHECK(historySize(hub) == 2);
    const auto now = hub.processor.slotsSnapshot().slot(0)->pattern;
    CHECK(now->voices[1].notes != original.voices[1].notes);
    CHECK(now->voices[0].notes == original.voices[0].notes);
    pulse(voice, "variation_all");
    CHECK(historySize(hub) == 3);
    // the voice kept nothing of its own
    CHECK(sameNotes(*voice.processor.slotsSnapshot().slot(0)->pattern, *hub.processor.slotsSnapshot().slot(0)->pattern));
    // and a voice without a hub has nobody to ask
    Instance lonely("Lonely");
    lonely.setRole(InstanceRole::Voice);
    hub.setRole(InstanceRole::Solo);
    pulse(lonely, "variation_1");
    CHECK(lonely.processor.generationStatus() == mm::plugin::GenerationStatus::UseHub);
}

TEST_CASE("trigger parameters: variation all changes more than one voice does", "[group-wiring][trigger]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) { settings.variationStrength = 100; });
    size_t one = 0;
    size_t all = 0;
    for (const char* id : {"variation_1", "variation_all"}) {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 36))); });
        pulse(hub, id);
        (std::string(id) == "variation_all" ? all : one) = hub.processor.lastVariationChanges();
    }
    CHECK(one > 0);
    CHECK(all > one);
}

// --- the chromatic share (SPEC 3.8, D-219) ---------------------------------------------------------------------

TEST_CASE("the chromatic share is saved with the instance, only when chosen, and kept in range", "[group-wiring][chromatic]") {
    Quiet quiet;
    Instance source("Source");
    CHECK_FALSE(source.processor.instanceSettings().generation.chromaticPct.has_value());
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("genChromatic")); // states of the style's share stay as they were
    auto settings = source.processor.instanceSettings();
    settings.generation.chromaticPct = 45;
    source.processor.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.instanceSettings().generation.chromaticPct == std::optional<int>(45));
    settings.generation.chromaticPct = 0; // 0 % is a choice
    source.processor.setInstanceSettings(settings);
    CHECK(source.processor.instanceSettings().generation.chromaticPct == std::optional<int>(0));
    settings.generation.chromaticPct = 400;
    source.processor.setInstanceSettings(settings);
    CHECK(source.processor.instanceSettings().generation.chromaticPct == std::optional<int>(100));
    settings.generation.chromaticPct = -7;
    source.processor.setInstanceSettings(settings);
    CHECK(source.processor.instanceSettings().generation.chromaticPct == std::optional<int>(0));
    // and a state without the attribute loads as the style's
    target.processor.setStateInformation(standard.getData(), static_cast<int>(standard.getSize()));
    CHECK_FALSE(target.processor.instanceSettings().generation.chromaticPct.has_value());
}

TEST_CASE("a pattern is generated with the chromatic share of the instance and remembers it", "[group-wiring][chromatic]") {
    Quiet quiet;
    for (const int percent : {0, 90}) {
        Instance hub("Hub");
        hub.setRole(InstanceRole::Hub);
        auto settings = hub.processor.instanceSettings();
        settings.generation.chromaticPct = percent;
        hub.processor.setInstanceSettings(settings);
        hub.processor.generate();
        INFO("percent " << percent);
        const bool filled = waitFor([&] { return !hub.processor.slotsSnapshot().isEmpty(0); });
        INFO("status " << static_cast<int>(hub.processor.generationStatus()));
        REQUIRE(filled);
        CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->info.chromaticPct == percent);
        // a variation keeps it: the constraint layer keeps working with the share of the pattern
        REQUIRE(hub.processor.vary(100, std::nullopt, std::nullopt, std::nullopt));
        CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->info.chromaticPct == percent);
    }
    Instance plain("Plain");
    plain.setRole(InstanceRole::Hub);
    plain.processor.generate();
    REQUIRE(waitFor([&] { return !plain.processor.slotsSnapshot().isEmpty(0); }));
    CHECK(plain.processor.slotsSnapshot().slot(0)->pattern->info.chromaticPct == -1);
}

// --- keyboard shortcuts of the user (SPEC 8.3, D-235) ---------------------------------------------------------------

namespace {

bool pressKey(juce::AudioProcessorEditor& editor, const char* chord) {
    const auto parsed = mm::core::parseChord(chord);
    REQUIRE(parsed.has_value());
    return dynamic_cast<mm::plugin::PlaceholderEditor&>(editor).keyPressed(mm::plugin::keyPressOf(*parsed));
}

mm::plugin::EditableRoll* firstRoll(juce::AudioProcessorEditor& editor) {
    for (auto* component : editor.getChildren()) {
        if (auto* row = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
            return &row->roll();
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("a key without an assignment is left to the host", "[group-wiring][shortcuts][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    for (const char* chord : {"cmd+z", "f5", "k", "cmd+shift+g", "escape", "delete", "left"}) {
        CHECK_FALSE(pressKey(*editor, chord)); // nothing is assigned: MidiMaid takes no key
    }
    store.update([](mm::core::GlobalSettings& settings) { settings.shortcuts = {{"focus_1", "cmd+1"}}; });
    CHECK(pressKey(*editor, "cmd+1"));
    CHECK_FALSE(pressKey(*editor, "cmd+2"));
    CHECK_FALSE(pressKey(*editor, "cmd+shift+1")); // another chord
    CHECK_FALSE(pressKey(*editor, "1"));
}

TEST_CASE("an assigned key switches the focus, the grid, the snapping and the fold", "[group-wiring][shortcuts][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) {
        settings.shortcuts = {{"focus_1", "f1"},         {"focus_2", "f2"},          {"focus_all", "f3"},
                              {"grid_finer", "alt+up"},  {"grid_coarser", "alt+down"}, {"triplets", "alt+t"},
                              {"snap_toggle", "alt+s"},  {"fold_toggle", "alt+f"}};
    });
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, markedTwoVoices(40, 60))); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    hub.step();
    pumpMessages(80);
    auto& placeholder = dynamic_cast<mm::plugin::PlaceholderEditor&>(*editor);
    CHECK(placeholder.focusedVoice() == 0);
    CHECK(pressKey(*editor, "f1"));
    CHECK(placeholder.focusedVoice() == 1);
    CHECK(pressKey(*editor, "f2"));
    CHECK(placeholder.focusedVoice() == 2);
    CHECK(pressKey(*editor, "f3"));
    CHECK(placeholder.focusedVoice() == 0);

    auto* grid = child<juce::ComboBox>(*editor, "grid_box");
    REQUIRE(grid != nullptr);
    const int division = grid->getSelectedId();
    CHECK(pressKey(*editor, "alt+up")); // finer: a larger division
    CHECK(grid->getSelectedId() > division);
    CHECK(pressKey(*editor, "alt+down"));
    CHECK(grid->getSelectedId() == division);
    const int smallest = [&] {
        int low = division;
        for (int i = 0; i < grid->getNumItems(); ++i) {
            low = std::min(low, grid->getItemId(i));
        }
        return low;
    }();
    grid->setSelectedId(smallest, juce::sendNotificationSync);
    CHECK(pressKey(*editor, "alt+down")); // the key is taken even at the end of the list
    CHECK(grid->getSelectedId() == smallest);

    auto* triplet = child<juce::ToggleButton>(*editor, "triplet");
    REQUIRE(triplet != nullptr);
    const bool before = triplet->getToggleState();
    CHECK(pressKey(*editor, "alt+t"));
    CHECK(triplet->getToggleState() != before);
    CHECK(pressKey(*editor, "alt+t"));
    CHECK(triplet->getToggleState() == before);

    auto* snapBox = child<juce::ComboBox>(*editor, "snap_box");
    REQUIRE(snapBox != nullptr);
    const int snapBefore = snapBox->getSelectedId();
    CHECK(pressKey(*editor, "alt+s"));
    CHECK(snapBox->getSelectedId() != snapBefore);

    CHECK_FALSE(store.get().rollFold);
    CHECK(pressKey(*editor, "alt+f"));
    CHECK(store.get().rollFold);
    CHECK(firstRoll(*editor)->fold());
    CHECK(pressKey(*editor, "alt+f"));
    CHECK_FALSE(store.get().rollFold);
    CHECK_FALSE(firstRoll(*editor)->fold());
}

TEST_CASE("assigned keys edit the notes of the roll", "[group-wiring][shortcuts][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) {
        settings.shortcuts = {{"select_all", "cmd+a"},   {"delete", "backspace"},   {"duplicate", "cmd+d"},
                              {"pitch_up", "shift+up"},  {"pitch_down", "shift+down"}, {"octave_up", "alt+up"},
                              {"octave_down", "alt+down"}, {"move_right", "shift+right"}, {"move_left", "shift+left"},
                              {"longer", "alt+right"},   {"shorter", "alt+left"},   {"accent", "cmd+k"},
                              {"slide", "cmd+l"},        {"undo", "cmd+z"},         {"redo", "cmd+shift+z"}};
    });
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    mm::core::Pattern pattern = mm::core::makeEmptyPattern(1, "peak_time");
    REQUIRE(mm::core::addNote(pattern, 0, 45, 0, 480, 100) != 0);
    REQUIRE(mm::core::addNote(pattern, 0, 48, 960, 480, 100) != 0);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    hub.step();
    pumpMessages(80);
    const auto bass = [&] { return hub.processor.slotsSnapshot().slot(0)->pattern->voices[0].notes; };
    auto* roll = firstRoll(*editor);
    REQUIRE(roll != nullptr);
    const auto before = bass();

    CHECK(pressKey(*editor, "cmd+a"));
    CHECK(roll->selection().size() == 2);
    CHECK(pressKey(*editor, "shift+up")); // a scale step: the first note is the A of A minor, next is B
    CHECK(bass()[0].pitch == 47);
    CHECK(bass()[1].pitch == 50);
    CHECK(pressKey(*editor, "shift+down"));
    CHECK(bass()[0].pitch == 45);
    CHECK(pressKey(*editor, "alt+up"));
    CHECK(bass()[0].pitch == 57);
    CHECK(pressKey(*editor, "alt+down"));
    CHECK(bass()[0].pitch == 45);
    CHECK(pressKey(*editor, "shift+right")); // one step of the grid (a 16th by default)
    CHECK(bass()[0].startTick == 240);
    CHECK(pressKey(*editor, "shift+left"));
    CHECK(bass()[0].startTick == 0);
    CHECK(pressKey(*editor, "alt+right"));
    CHECK(bass()[0].lengthTicks == 720);
    CHECK(pressKey(*editor, "alt+left"));
    CHECK(bass()[0].lengthTicks == 480);
    CHECK(pressKey(*editor, "cmd+k"));
    CHECK(bass()[0].accent);
    CHECK(pressKey(*editor, "cmd+l"));
    CHECK(bass()[0].slide);
    CHECK(pressKey(*editor, "cmd+z")); // undo the slide
    CHECK_FALSE(bass()[0].slide);
    CHECK(pressKey(*editor, "cmd+shift+z")); // and redo it
    CHECK(bass()[0].slide);
    CHECK(pressKey(*editor, "cmd+d"));
    CHECK(bass().size() == 4);
    CHECK(pressKey(*editor, "backspace"));
    CHECK(bass().size() == 2); // the selection is the copies: they go
    CHECK(bass().size() == before.size());
}

TEST_CASE("key presses become chords and back", "[shortcuts][keys]") {
    for (const char* text : {"cmd+k", "shift+k", "alt+k", "cmd+shift+alt+k", "k", "5", "f1", "f12", "f20", "left", "right", "up",
                             "down", "home", "end", "pageup", "pagedown", "delete", "backspace", "return", "tab", "escape",
                             "cmd+[", "cmd+.", "alt+,"}) {
        INFO(text);
        const auto chord = mm::core::parseChord(text);
        REQUIRE(chord.has_value());
        const auto press = mm::plugin::keyPressOf(*chord);
        const auto back = mm::plugin::chordOf(press);
        REQUIRE(back.has_value());
        CHECK(*back == *chord);
    }
    // the space bar and a lone modifier are no chords
    CHECK_FALSE(mm::plugin::chordOf(juce::KeyPress(juce::KeyPress::spaceKey)).has_value());
    CHECK_FALSE(mm::plugin::chordOf(juce::KeyPress(juce::KeyPress::spaceKey, juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0)).has_value());
    CHECK_FALSE(mm::plugin::chordOf(juce::KeyPress()).has_value());
    CHECK_FALSE(mm::plugin::chordOf(juce::KeyPress(juce::KeyPress::numberPad0)).has_value());
    // a letter is the same with and without caps
    CHECK(mm::plugin::chordOf(juce::KeyPress('K', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0)) ==
          mm::plugin::chordOf(juce::KeyPress('k', juce::ModifierKeys(juce::ModifierKeys::commandModifier), 0)));
    // the text shown to the user
    CHECK(mm::plugin::chordText(*mm::core::parseChord("cmd+shift+k")).endsWith("Shift+K"));
    CHECK(mm::plugin::chordText(*mm::core::parseChord("f5")) == "F5");
    CHECK(mm::plugin::chordText(*mm::core::parseChord("alt+left")) == "Alt+Left");
}

TEST_CASE("assigned keys generate, vary and take steps through the history", "[group-wiring][shortcuts][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) {
        settings.shortcuts = {{"generate", "cmd+g"}, {"vary", "cmd+v"}, {"history_back", "cmd+["}, {"history_forward", "cmd+]"},
                              {"undo", "cmd+z"},     {"redo", "cmd+y"},  {"refine_send", "cmd+r"}};
    });
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    CHECK(pressKey(*editor, "cmd+z")); // assigned, so the key is taken, even with nothing to undo
    hub.processor.generate();
    REQUIRE(waitFor([&] { return hub.processor.slotsSnapshot().slot(0)->pattern.has_value(); }, 20000));
    const auto first = *hub.processor.slotsSnapshot().slot(0)->pattern;
    CHECK(pressKey(*editor, "cmd+v")); // a variation of all voices
    REQUIRE(waitFor([&] { return *hub.processor.slotsSnapshot().slot(0)->pattern != first; }));
    CHECK(pressKey(*editor, "cmd+z"));
    CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->voices == first.voices); // (the version counter goes on)
    CHECK(pressKey(*editor, "cmd+y"));
    CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->voices != first.voices);
    CHECK(pressKey(*editor, "cmd+g")); // a new pattern
    REQUIRE(waitFor([&] { return hub.processor.slotsSnapshot().slot(0)->history.size() >= 2; }, 20000));
    pumpMessages(80);
    const auto latest = *hub.processor.slotsSnapshot().slot(0)->pattern;
    CHECK(pressKey(*editor, "cmd+["));
    CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->voices != latest.voices);
    CHECK(pressKey(*editor, "cmd+]"));
    CHECK(hub.processor.slotsSnapshot().slot(0)->pattern->voices == latest.voices);
    // without a text for the refinement nothing is sent, the key is taken all the same
    CHECK(pressKey(*editor, "cmd+r"));
    auto& placeholder = dynamic_cast<mm::plugin::PlaceholderEditor&>(*editor);
    CHECK_FALSE(placeholder.runShortcut("no_such_action"));
    CHECK_FALSE(placeholder.runShortcut(""));
}

TEST_CASE("the voice window takes no keys", "[group-wiring][shortcuts][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempSettings temp;
    auto& store = temp.open();
    REQUIRE(waitFor([&] { return store.ready(); }));
    store.update([](mm::core::GlobalSettings& settings) { settings.shortcuts = {{"focus_1", "f1"}}; });
    Quiet quiet;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> editor(voice.processor.createEditor());
    pumpMessages(60);
    CHECK_FALSE(pressKey(*editor, "f1"));
}

TEST_CASE("the chromatic slider is an expert control that follows the instance", "[group-wiring][chromatic][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    auto* slider = child<juce::Slider>(*editor, "chromatic");
    REQUIRE(slider != nullptr);
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK_FALSE(slider->isVisible()); // basic level
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(true, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK(slider->isVisible());
    CHECK(slider->getValue() == -1.0);
    CHECK(slider->getTextFromValue(-1.0) == "Stil"); // the style's share
    CHECK(slider->getTextFromValue(35.0) == "35 %");
    CHECK(editor->getLocalBounds().contains(slider->getBounds()));
    for (auto* other : editor->getChildren()) {
        if (other != slider && other->isVisible() && other->getWidth() > 0 &&
            dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
            INFO(other->getComponentID().toStdString());
            CHECK_FALSE(slider->getBounds().intersects(other->getBounds()));
        }
    }
    slider->setValue(60.0, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.chromaticPct == std::optional<int>(60));
    slider->setValue(0.0, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.chromaticPct == std::optional<int>(0)); // 0 % is a choice, not "Stil"
    slider->setValue(-1.0, juce::sendNotificationSync);
    CHECK_FALSE(hub.processor.instanceSettings().generation.chromaticPct.has_value());
    // a change of the instance (a loaded state) moves the slider
    auto settings = hub.processor.instanceSettings();
    settings.generation.chromaticPct = 20;
    hub.processor.setInstanceSettings(settings);
    CHECK(waitFor([&] { return slider->getValue() == 20.0; }));
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
}

// --- the hat mode of the melody (SPEC 3.14, D-228) --------------------------------------------------------------

TEST_CASE("the hat mode is saved with the instance, only when chosen", "[group-wiring][hatmode]") {
    Quiet quiet;
    Instance source("Source");
    CHECK(source.processor.instanceSettings().generation.hatMode == mm::core::HatMode::Free);
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("genHatMode")); // states from before stay as they were
    for (const auto mode : {mm::core::HatMode::With, mm::core::HatMode::Against}) {
        auto settings = source.processor.instanceSettings();
        settings.generation.hatMode = mode;
        source.processor.setInstanceSettings(settings);
        juce::MemoryBlock saved;
        source.processor.getStateInformation(saved);
        Instance target("Target");
        target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        CHECK(target.processor.instanceSettings().generation.hatMode == mode);
        // a state without the attribute loads as free
        target.processor.setStateInformation(standard.getData(), static_cast<int>(standard.getSize()));
        CHECK(target.processor.instanceSettings().generation.hatMode == mm::core::HatMode::Free);
    }
}

TEST_CASE("the hat mode box is an expert control that follows the instance", "[group-wiring][hatmode][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    auto* box = child<juce::ComboBox>(*editor, "hatMode");
    REQUIRE(box != nullptr);
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK_FALSE(box->isVisible()); // basic level
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(true, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK(box->isVisible());
    CHECK(box->getNumItems() == 3);
    CHECK(box->getItemText(0) == "Frei");
    CHECK(box->getItemText(1) == "Mit Hats");
    CHECK(box->getItemText(2) == "Gegen Hats");
    CHECK(editor->getLocalBounds().contains(box->getBounds()));
    for (auto* other : editor->getChildren()) {
        if (other != box && other->isVisible() && other->getWidth() > 0 &&
            dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
            INFO(other->getComponentID().toStdString());
            CHECK_FALSE(box->getBounds().intersects(other->getBounds()));
        }
    }
    box->setSelectedId(2, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.hatMode == mm::core::HatMode::With);
    box->setSelectedId(3, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.hatMode == mm::core::HatMode::Against);
    box->setSelectedId(1, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.hatMode == mm::core::HatMode::Free);
    // a change of the instance (a loaded state) moves the box
    auto settings = hub.processor.instanceSettings();
    settings.generation.hatMode = mm::core::HatMode::Against;
    hub.processor.setInstanceSettings(settings);
    CHECK(waitFor([&] { return box->getSelectedId() == 3; }));
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
}

// --- the preview with the internal clock (SPEC 3.11, D-236) ----------------------------------------------------------

TEST_CASE("the preview plays the slot while the host stands still and ends when the host starts", "[group-wiring][preview]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    fillSlots(hub);
    hub.setPlaying(false);
    for (int i = 0; i < 400; ++i) {
        hub.step();
    }
    CHECK(hub.notes().empty()); // the host stands still: nothing
    CHECK_FALSE(hub.processor.previewing());
    hub.processor.setPreview(true);
    CHECK(hub.processor.previewing());
    for (int i = 0; i < 400; ++i) { // two bars at 120 BPM and 512 samples
        hub.step();
    }
    CHECK(hub.notes().size() >= 2);
    for (const auto& note : hub.notes()) {
        CHECK(note.pitch == 41);
    }
    CHECK(hub.processor.previewing());
    // the host starts: the preview ends, the position of the host takes over
    const size_t before = hub.notes().size();
    hub.setPpq(8.0);
    hub.setPlaying(true);
    hub.step();
    CHECK_FALSE(hub.processor.previewing());
    for (int i = 0; i < 100; ++i) {
        hub.step();
    }
    CHECK(hub.notes().size() >= before); // and it plays on with the transport
    // a stop of the preview ends the notes: no new ones
    hub.setPlaying(false);
    hub.processor.setPreview(true);
    hub.step();
    const size_t running = hub.notes().size();
    hub.processor.setPreview(false);
    for (int i = 0; i < 400; ++i) {
        hub.step();
    }
    CHECK(hub.notes().size() == running);
}

TEST_CASE("a voice joins a running preview at the position of the hub", "[group-wiring][preview]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    hub.setPlaying(false);
    voice.setPlaying(false);
    hub.processor.setPreview(true);
    for (int i = 0; i < 300; ++i) { // 6 PPQ: the hub is in the middle of its second bar
        hub.step();
    }
    voice.step(); // the voice sees the preview of the hub for the first time
    CHECK(voice.notes().empty()); // it did not start at 0 (that note would sound at once): it starts where the hub is
    for (int i = 0; i < 100; ++i) {
        hub.step();
        voice.step();
    }
    CHECK_FALSE(voice.notes().empty()); // and plays the next bar line
}

TEST_CASE("the preview keeps the notes balanced: every note-on gets its note-off", "[group-wiring][preview]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([](mm::core::SlotBank& bank) {
        mm::core::Pattern pattern = mm::core::makeEmptyPattern(1, "peak_time");
        mm::core::Note note;
        note.id = mm::core::allocateNoteId(pattern);
        note.pitch = 45;
        note.startTick = 960;
        note.lengthTicks = 1920; // sounds a long while
        pattern.voices[0].notes.push_back(note);
        REQUIRE(bank.setResult(0, pattern));
    });
    hub.setPlaying(false);
    hub.processor.setPreview(true);
    for (int i = 0; i < 70; ++i) { // into the note
        hub.step();
    }
    REQUIRE(hub.notes().size() == 1);
    CHECK(hub.noteOffs().empty());
    hub.processor.setPreview(false); // stopped while sounding
    hub.step();
    CHECK(hub.noteOffs().size() == 1);
}

TEST_CASE("the preview of the hub takes its voices along", "[group-wiring][preview]") {
    Quiet quiet;
    Instance hub("Hub");
    Instance voice("Voice");
    hub.setRole(InstanceRole::Hub);
    voice.setRole(InstanceRole::Voice);
    fillSlots(hub);
    hub.setPlaying(false);
    voice.setPlaying(false);
    for (int i = 0; i < 100; ++i) {
        hub.step();
        voice.step();
    }
    CHECK(hub.notes().empty());
    CHECK(voice.notes().empty());
    hub.processor.setPreview(true);
    for (int i = 0; i < 400; ++i) {
        hub.step();
        voice.step();
    }
    CHECK(hub.notes().size() >= 2);
    CHECK(voice.notes().size() >= 2); // the voice follows with its own clock
    for (const auto& note : voice.notes()) {
        CHECK(note.pitch == 41);
    }
    CHECK_FALSE(voice.processor.previewing()); // the voice has no button of its own
    // the hub ends the preview: the voice ends too
    hub.processor.setPreview(false);
    hub.step();
    voice.step();
    const size_t heard = voice.notes().size();
    for (int i = 0; i < 400; ++i) {
        hub.step();
        voice.step();
    }
    CHECK(voice.notes().size() == heard);
    CHECK(hub.notes().size() >= 2);
}

TEST_CASE("the preview button of the hub switches the preview and shows its state", "[group-wiring][preview][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    auto* button = child<juce::TextButton>(*editor, "preview");
    REQUIRE(button != nullptr);
    CHECK(button->isVisible());
    CHECK(button->getButtonText() == "Vorhören");
    CHECK(editor->getLocalBounds().contains(button->getBounds()));
    for (auto* other : editor->getChildren()) {
        if (other != button && other->isVisible() && other->getWidth() > 0 &&
            dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
            INFO(other->getComponentID().toStdString());
            CHECK_FALSE(button->getBounds().intersects(other->getBounds()));
        }
    }
    button->onClick();
    CHECK(hub.processor.previewing());
    CHECK(button->getButtonText() == "Stopp");
    button->onClick();
    CHECK_FALSE(hub.processor.previewing());
    CHECK(button->getButtonText() == "Vorhören");
    // the end of the preview by the host shows in the button
    hub.processor.setPreview(true);
    CHECK(waitFor([&] { return button->getButtonText() == "Stopp"; }));
    hub.setPpq(8.0);
    hub.setPlaying(true);
    hub.step();
    CHECK(waitFor([&] { return button->getButtonText() == "Vorhören"; }));
}

// --- how a new pattern starts: restart or legato (SPEC 3.4, D-236) -------------------------------------------------

TEST_CASE("the start mode is saved with the instance, only when it is legato", "[group-wiring][legato]") {
    Quiet quiet;
    Instance source("Source");
    CHECK_FALSE(source.processor.instanceSettings().legato);
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("switchLegato")); // states from before stay as they were
    auto settings = source.processor.instanceSettings();
    settings.legato = true;
    source.processor.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.instanceSettings().legato);
    target.processor.setStateInformation(standard.getData(), static_cast<int>(standard.getSize()));
    CHECK_FALSE(target.processor.instanceSettings().legato);
}

TEST_CASE("the start box is an expert control that follows the instance", "[group-wiring][legato][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    auto* box = child<juce::ComboBox>(*editor, "startMode");
    REQUIRE(box != nullptr);
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK_FALSE(box->isVisible()); // basic level
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(true, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK(box->isVisible());
    CHECK(box->getNumItems() == 2);
    CHECK(box->getItemText(0) == "Einsatz: Neustart");
    CHECK(box->getItemText(1) == "Einsatz: Legato");
    CHECK(editor->getLocalBounds().contains(box->getBounds()));
    for (auto* other : editor->getChildren()) {
        if (other != box && other->isVisible() && other->getWidth() > 0 &&
            dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
            INFO(other->getComponentID().toStdString());
            CHECK_FALSE(box->getBounds().intersects(other->getBounds()));
        }
    }
    box->setSelectedId(2, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().legato);
    box->setSelectedId(1, juce::sendNotificationSync);
    CHECK_FALSE(hub.processor.instanceSettings().legato);
    auto settings = hub.processor.instanceSettings();
    settings.legato = true;
    hub.processor.setInstanceSettings(settings);
    CHECK(waitFor([&] { return box->getSelectedId() == 2; }));
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
}

// --- groove templates (SPEC 3.9, D-220) -----------------------------------------------------------------------

namespace {

/// A hub with a one-bar pattern whose bass plays a 16th on every step (the voice of the hub is the bass).
struct GrooveRig {
    Quiet quiet;
    Instance hub{"Hub"};
    GrooveRig() {
        hub.setRole(InstanceRole::Hub);
        mm::core::Pattern pattern = mm::core::makeEmptyPattern(1, "peak_time");
        for (uint32_t step = 0; step < 16; ++step) {
            mm::core::Note note;
            note.id = mm::core::allocateNoteId(pattern);
            note.pitch = 40;
            note.startTick = step * 240;
            note.lengthTicks = 60;
            pattern.voices[0].notes.push_back(note);
        }
        hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, pattern)); });
        hub.setSlotParameter(1);
    }
    mm::core::Track track() { return hub.processor.slotsSnapshot().slot(0)->pattern->voices[0]; } // a copy
    /// The sample of the first note-on at or after a position of the PPQ grid.
    long long noteNear(double ppq) const {
        long long best = -1;
        for (const auto& note : hub.notes()) {
            if (best < 0 || std::llabs(note.sample - at(ppq)) < std::llabs(best - at(ppq))) {
                best = note.sample;
            }
        }
        return best;
    }
};

} // namespace

TEST_CASE("a voice's groove is set like an edit: it plays at once and is an undo step", "[group-wiring][groove]") {
    GrooveRig rig;
    CHECK(rig.track().groove.templateId.empty()); // an empty id is straight
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("swing_62"), std::nullopt));
    CHECK(rig.track().groove.templateId == "swing_62");
    CHECK(rig.hub.processor.canUndo());
    REQUIRE(rig.hub.processor.undo());
    CHECK(rig.track().groove.templateId.empty());
    // the swing alone, in range
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.58f));
    CHECK(rig.track().groove.swing == 0.58f);
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.9f));
    CHECK(rig.track().groove.swing == 0.75f); // limited
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.1f));
    CHECK(rig.track().groove.swing == 0.5f);
    // the drum reference is a template too (it needs the reference to do anything)
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("drum_reference"), std::nullopt));
    CHECK(rig.track().groove.templateId == "drum_reference");
    // what does not exist or changes nothing
    CHECK_FALSE(rig.hub.processor.setVoiceGroove(0, 0, std::string("no_such_groove"), std::nullopt));
    CHECK_FALSE(rig.hub.processor.setVoiceGroove(0, 0, std::string("drum_reference"), std::nullopt)); // already
    CHECK_FALSE(rig.hub.processor.setVoiceGroove(0, 9, std::string("swing_62"), std::nullopt));        // no such voice
    CHECK_FALSE(rig.hub.processor.setVoiceGroove(5, 0, std::string("swing_62"), std::nullopt));        // an empty slot
}

TEST_CASE("a voice instance does not change the groove, it shows the hub's", "[group-wiring][groove]") {
    GrooveRig rig;
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    CHECK_FALSE(voice.processor.setVoiceGroove(0, 0, std::string("swing_62"), std::nullopt));
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("shuffle"), std::nullopt));
    CHECK(voice.processor.slotsSnapshot().slot(0)->pattern->voices[0].groove.templateId == "shuffle");
}

TEST_CASE("the view of a voice carries its groove: template, swing and whether the control works", "[group-wiring][groove]") {
    GrooveRig rig;
    auto views = rig.hub.processor.playingVoices();
    REQUIRE(views.size() >= 2);
    CHECK(views[0].grooveId == "straight");
    CHECK(views[0].swingActive);
    CHECK_FALSE(views[0].drumReference);
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("swing_66"), 0.54f));
    views = rig.hub.processor.playingVoices();
    CHECK(views[0].grooveId == "swing_66");
    CHECK_FALSE(views[0].swingActive);
    CHECK(views[0].swing == 0.66f); // what the template does
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("laid_back"), std::nullopt));
    views = rig.hub.processor.playingVoices();
    CHECK(views[0].swingActive);
    CHECK(views[0].swing == 0.54f);
}

TEST_CASE("the engine plays a groove template: the notes are where the template puts them", "[group-wiring][groove]") {
    GrooveRig rig;
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("swing_66"), std::nullopt));
    rig.hub.setPpq(0.0);
    runTo({&rig.hub}, 8.0);
    // from the second bar on the template plays (a result waits for its bar line): the odd 16th at 4.25 is 77 ticks late
    const double late = 4.0 + 0.25 + 77.0 / 960.0;
    CHECK(std::llabs(rig.noteNear(late) - at(late)) <= 2);
    // a straight 16th stays on its grid: 4.5
    bool onGrid = false;
    for (const auto& note : rig.hub.notes()) {
        onGrid = onGrid || std::llabs(note.sample - at(4.5)) <= 2;
    }
    CHECK(onGrid);
    // and not at the straight position of the odd 16th
    bool straightOdd = false;
    for (const auto& note : rig.hub.notes()) {
        straightOdd = straightOdd || std::llabs(note.sample - at(4.25)) <= 2;
    }
    CHECK_FALSE(straightOdd);
}

TEST_CASE("the voice row has the groove box and the swing slider", "[group-wiring][groove][editor]") {
    GrooveRig rig;
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.hub.processor.createEditor());
    pumpMessages(80);
    auto* box = child<juce::ComboBox>(*editor, "groove_1");
    auto* slider = child<juce::Slider>(*editor, "swing_1");
    REQUIRE((box != nullptr && slider != nullptr));
    REQUIRE(waitFor([&] { return box->getNumItems() > 1 && box->isEnabled(); }));
    CHECK(box->getItemText(0) == "Gerade");
    CHECK(box->getNumItems() == 1 + 8); // straight and the eight templates (no drum reference in this pattern)
    CHECK(box->getSelectedId() == 1);
    CHECK(slider->isEnabled());
    // choose a template with swing of its own: the pattern changes, the slider is off and shows the swing of the template
    int id = 0;
    for (int i = 0; i < box->getNumItems(); ++i) {
        if (box->getItemText(i) == "Swing 62 %") {
            id = box->getItemId(i);
        }
    }
    REQUIRE(id > 0);
    box->setSelectedId(id, juce::sendNotificationSync);
    CHECK(rig.track().groove.templateId == "swing_62");
    CHECK(waitFor([&] { return !slider->isEnabled() && slider->getValue() == 62.0; }));
    // Laid back works with the slider
    for (int i = 0; i < box->getNumItems(); ++i) {
        if (box->getItemText(i) == "Laid back") {
            id = box->getItemId(i);
        }
    }
    box->setSelectedId(id, juce::sendNotificationSync);
    CHECK(waitFor([&] { return slider->isEnabled(); }));
    slider->setValue(57.0, juce::sendNotificationSync);
    CHECK(std::abs(rig.track().groove.swing - 0.57f) < 1e-6f);
    // back to straight
    box->setSelectedId(1, juce::sendNotificationSync);
    CHECK(rig.track().groove.templateId == "straight");
}

TEST_CASE("a bass with swing over 56 percent shows the flam hint", "[group-wiring][groove][editor]") {
    GrooveRig rig;
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.hub.processor.createEditor());
    pumpMessages(80);
    auto* hint = child<juce::Label>(*editor, "flamHint_1");
    REQUIRE(hint != nullptr);
    CHECK(hint->getText() == "Flams mit der geraden Kick möglich");
    CHECK_FALSE(hint->isVisible());
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.56f));
    pumpMessages(80);
    CHECK_FALSE(hint->isVisible()); // 56 % is the border
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.57f));
    CHECK(waitFor([&] { return hint->isVisible(); }));
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.5f));
    CHECK(waitFor([&] { return !hint->isVisible(); }));
}

TEST_CASE("the drum reference is offered only when the pattern has one", "[group-wiring][groove][editor]") {
    GrooveRig rig;
    rig.hub.processor.editSlots([&](mm::core::SlotBank& bank) {
        mm::core::Pattern pattern = *bank.slot(0)->pattern;
        mm::core::RhythmReference ref;
        ref.bars = 1;
        pattern.rhythmRef = ref;
        REQUIRE(bank.setResult(0, pattern));
    });
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.hub.processor.createEditor());
    pumpMessages(80);
    auto* box = child<juce::ComboBox>(*editor, "groove_1");
    REQUIRE(box != nullptr);
    REQUIRE(waitFor([&] { return box->getNumItems() > 1; }));
    CHECK(box->getNumItems() == 2 + 8);
    CHECK(box->getItemText(1) == "Drum-Referenz");
    box->setSelectedId(2, juce::sendNotificationSync);
    CHECK(rig.track().groove.templateId == "drum_reference");
}

TEST_CASE("the groove controls fit in the voice rows and overlap nothing", "[group-wiring][groove][editor][layout]") {
    GrooveRig rig;
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.hub.processor.createEditor());
    pumpMessages(80);
    for (const int voice : {1, 2}) {
        auto* row = child<mm::plugin::VoiceRow>(*editor, "row_" + juce::String(voice));
        REQUIRE(row != nullptr);
        for (const char* prefix : {"groove_", "swing_"}) {
            auto* one = child<juce::Component>(*row, prefix + juce::String(voice));
            REQUIRE(one != nullptr);
            INFO(prefix << voice);
            CHECK(one->isVisible());
            CHECK(one->getHeight() >= 16);
            CHECK(one->getWidth() >= 100);
            CHECK(row->getLocalBounds().contains(one->getBounds()));
            // they sit in the title strip of the roll, over it, and over nothing else
            auto* roll = child<mm::plugin::EditableRoll>(*row, "roll_" + juce::String(voice));
            if (roll != nullptr) {
                CHECK(one->getBottom() <= roll->getY() + mm::plugin::EditableRoll::kTitleHeight);
                CHECK(one->getY() >= roll->getY());
            }
            for (auto* other : row->getChildren()) {
                if (other != one && other != roll && other->isVisible() && other->getWidth() > 0) {
                    INFO(other->getComponentID().toStdString());
                    CHECK_FALSE(one->getBounds().intersects(other->getBounds()));
                }
            }
        }
        // the grip below the buttons is still a handle
        auto* grip = child<mm::plugin::DragHandle>(*row, "drag_" + juce::String(voice));
        if (grip != nullptr) {
            CHECK(grip->getHeight() >= 24);
        }
    }
}

TEST_CASE("one drag of the swing slider is one undo step", "[group-wiring][groove]") {
    GrooveRig rig;
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.52f)); // a step of its own
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.55f, false)); // the drag begins a step ...
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.60f, true)); // ... and goes on in it
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.65f, true));
    CHECK(rig.track().groove.swing == 0.65f);
    REQUIRE(rig.hub.processor.undo()); // the whole drag at once
    CHECK(rig.track().groove.swing == 0.52f);
    REQUIRE(rig.hub.processor.undo());
    CHECK(rig.track().groove.swing == 0.5f);
    CHECK_FALSE(rig.hub.processor.canUndo());
}

TEST_CASE("evolve parameters: what the host wrote is not written back, the toggle of the window is", "[group-wiring][evolve][parameters]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    hub.processor.editSlots([&](mm::core::SlotBank& bank) { REQUIRE(bank.setResult(0, evolvePattern(hub, 41))); });
    auto* evolve = hub.processor.parameters().getParameter("evolve");
    evolve->setValueNotifyingHost(0.6f); // a host (pluginval, an automation lane) moves it to a value of its own
    pumpMessages(40);
    CHECK(hub.processor.evolveOn());
    CHECK(evolve->getValue() == 0.6f); // the plug-in does not race with the host by writing 1.0 over it
    evolve->setValueNotifyingHost(0.2f);
    pumpMessages(40);
    CHECK_FALSE(hub.processor.evolveOn());
    CHECK(evolve->getValue() == 0.2f);
    hub.processor.setEvolve(true); // the toggle of the window does write the parameter
    CHECK(evolve->getValue() == 1.0f);
    hub.processor.setEvolve(false);
    CHECK(evolve->getValue() == 0.0f);
}

TEST_CASE("the hub shows the reference sets of its style and switches the active one", "[group-wiring][editor][sets]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::ReferenceStore references{juce::File(), mm::plugin::embeddedStyles()};
    auto* previous = &mm::plugin::referenceStore();
    mm::plugin::overrideReferenceStore(&references);
    struct Restore {
        mm::plugin::ReferenceStore* previous;
        ~Restore() { mm::plugin::overrideReferenceStore(previous); }
    } restore{previous};
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    auto* box = child<juce::ComboBox>(*editor, "referenceSet");
    REQUIRE(box != nullptr);
    CHECK(box->isVisible());
    CHECK(box->getNumItems() == 1);
    CHECK(box->getText() == "Favoriten");
    // a set that is made elsewhere (the references window) shows up at the next tick and is the active one
    const auto id = references.createSet("peak_time", "Warehouse");
    REQUIRE_FALSE(id.empty());
    CHECK(waitFor([&] { return box->getNumItems() == 2 && box->getText() == "Warehouse"; }));
    // the choice in the hub is the active set of the style, which every instance of the process uses
    box->setSelectedId(1, juce::sendNotificationSync);
    CHECK(references.collection("peak_time").activeSetId == mm::core::kFavoritesSetId);
    CHECK(references.get("peak_time").setId == mm::core::kFavoritesSetId);
    box->setSelectedId(2, juce::sendNotificationSync);
    CHECK(references.collection("peak_time").activeSetId == id);
    // another style has its own sets: the box follows the style (after the box has caught up with the store)
    pumpMessages(300);
    auto settings = hub.processor.instanceSettings();
    settings.generation.styleId = "melodic_techno";
    hub.processor.setInstanceSettings(settings);
    CHECK(waitFor([&] { return box->getNumItems() == 1; }));
    CHECK(box->getText() == "Favoriten");
    // a voice window has no set box
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice);
    std::unique_ptr<juce::AudioProcessorEditor> voiceEditor(voice.processor.createEditor());
    CHECK_FALSE(child<juce::ComboBox>(*voiceEditor, "referenceSet")->isVisible());
}

// --- the "Persönlich" setting (SPEC 3.20, D-241) ----------------------------------------------------------------

TEST_CASE("the personal setting is saved with the instance, only when changed, and kept in range", "[group-wiring][personal]") {
    Quiet quiet;
    Instance source("Source");
    CHECK(source.processor.instanceSettings().generation.personalPct == 50);
    juce::MemoryBlock standard;
    source.processor.getStateInformation(standard);
    auto root = juce::AudioProcessor::getXmlFromBinary(standard.getData(), static_cast<int>(standard.getSize()));
    REQUIRE(root != nullptr);
    CHECK_FALSE(root->hasAttribute("genPersonal")); // states of before stay as they were
    auto settings = source.processor.instanceSettings();
    settings.generation.personalPct = 80;
    source.processor.setInstanceSettings(settings);
    juce::MemoryBlock saved;
    source.processor.getStateInformation(saved);
    Instance target("Target");
    target.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.processor.instanceSettings().generation.personalPct == 80);
    settings.generation.personalPct = 0; // 0 % is a choice
    source.processor.setInstanceSettings(settings);
    CHECK(source.processor.instanceSettings().generation.personalPct == 0);
    juce::MemoryBlock zero;
    source.processor.getStateInformation(zero);
    target.processor.setStateInformation(zero.getData(), static_cast<int>(zero.getSize()));
    CHECK(target.processor.instanceSettings().generation.personalPct == 0);
    settings.generation.personalPct = 400;
    source.processor.setInstanceSettings(settings);
    CHECK(source.processor.instanceSettings().generation.personalPct == 100);
    settings.generation.personalPct = -7;
    source.processor.setInstanceSettings(settings);
    CHECK(source.processor.instanceSettings().generation.personalPct == 0);
    // a state without the attribute loads as 50
    target.processor.setStateInformation(standard.getData(), static_cast<int>(standard.getSize()));
    CHECK(target.processor.instanceSettings().generation.personalPct == 50);
}

TEST_CASE("the personal slider is an expert control that follows the instance", "[group-wiring][personal][editor]") {
    Quiet quiet;
    Instance hub("Hub");
    hub.setRole(InstanceRole::Hub);
    std::unique_ptr<juce::AudioProcessorEditor> editor(hub.processor.createEditor());
    pumpMessages(60);
    auto* slider = child<juce::Slider>(*editor, "personal");
    REQUIRE(slider != nullptr);
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(false, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK_FALSE(slider->isVisible()); // basic level
    child<juce::ToggleButton>(*editor, "expert")->setToggleState(true, juce::dontSendNotification);
    child<juce::ToggleButton>(*editor, "expert")->onClick();
    pumpMessages(40);
    CHECK(slider->isVisible());
    CHECK(slider->getValue() == 50.0);
    CHECK(slider->getTextFromValue(35.0) == "35 %");
    CHECK(editor->getLocalBounds().contains(slider->getBounds()));
    for (auto* other : editor->getChildren()) {
        if (other != slider && other->isVisible() && other->getWidth() > 0 &&
            dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
            INFO(other->getComponentID().toStdString());
            CHECK_FALSE(slider->getBounds().intersects(other->getBounds()));
        }
    }
    // five columns share the row: the slider and the hat mode both have room
    CHECK(slider->getWidth() > 60);
    CHECK(child<juce::ComboBox>(*editor, "hatMode")->getWidth() > 60);
    CHECK(slider->getRight() <= child<juce::ComboBox>(*editor, "hatMode")->getX());
    slider->setValue(80.0, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.personalPct == 80);
    slider->setValue(0.0, juce::sendNotificationSync);
    CHECK(hub.processor.instanceSettings().generation.personalPct == 0);
    auto settings = hub.processor.instanceSettings();
    settings.generation.personalPct = 20;
    hub.processor.setInstanceSettings(settings);
    CHECK(waitFor([&] { return slider->getValue() == 20.0; }));
    child<juce::ToggleButton>(*editor, "expert")->onClick();
}

// --- own groove templates (SPEC 3.9, D-245) ---------------------------------------------------------------------

namespace {

/// A fresh in-memory store of own templates, installed as the store of the process while it lives.
struct OwnGrooves {
    mm::plugin::UserGrooveStore store{juce::File(), mm::plugin::embeddedGrooves()};
    OwnGrooves() { mm::plugin::overrideUserGrooves(&store); }
    ~OwnGrooves() { mm::plugin::overrideUserGrooves(&mmtest::disabledUserGrooves()); }
};

/// What the voice plays: start, length, pitch and velocity of every note.
std::vector<std::array<uint32_t, 4>> playedNotes(GrooveRig& rig) {
    std::vector<std::array<uint32_t, 4>> played;
    for (const auto& note : rig.hub.processor.playingVoice().notes) {
        played.push_back({note.startTick, note.lengthTicks, note.pitch, note.velocity});
    }
    return played;
}

} // namespace

TEST_CASE("the store of own templates adds, names and keeps them in a folder", "[group-wiring][groove][own]") {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("mm_grooves_" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    struct Cleanup {
        juce::File folder;
        ~Cleanup() { folder.deleteRecursively(); }
    } cleanup{folder};

    mm::core::GrooveTemplate groove;
    groove.nameDe = "  Mein Groove ";
    groove.bars = 1;
    groove.offsetTicks.assign(16, 0);
    groove.offsetTicks[1] = 30;
    groove.velocityPermille.assign(16, 1000);
    std::string first;
    std::string second;
    {
        mm::plugin::UserGrooveStore store{folder, mm::plugin::embeddedGrooves()};
        store.waitForIdle();
        CHECK(waitFor([&] { return store.ready(); }));
        const auto before = store.library()->size();
        first = store.add(groove);
        CHECK(first == "user_mein_groove");
        second = store.add(groove); // the same name: another id
        CHECK(second == "user_mein_groove_2");
        CHECK(store.library()->size() == before + 2);
        REQUIRE(store.library()->find(first) != nullptr);
        CHECK(store.library()->find(first)->nameDe == "Mein Groove"); // trimmed
        CHECK_FALSE(store.library()->find(first)->swing);             // the template as it was given
        CHECK(store.library()->find(first)->offsetTicks[1] == 30);
        // what is not a template: nothing added
        auto bad = groove;
        bad.nameDe = "   ";
        CHECK(store.add(bad).empty());
        bad.nameDe = std::string(mm::plugin::UserGrooveStore::kMaxNameLength + 1, 'x');
        CHECK(store.add(bad).empty());
        bad = groove;
        bad.offsetTicks.assign(5, 0); // not one value per 16th
        CHECK(store.add(bad).empty());
        CHECK(store.library()->size() == before + 2);
        store.waitForIdle();
    }
    // a store that reads the folder again has them, after the embedded ones
    mm::plugin::UserGrooveStore again{folder, mm::plugin::embeddedGrooves()};
    CHECK(waitFor([&] { return again.ready(); }));
    CHECK(again.library()->find(first) != nullptr);
    CHECK(again.library()->find(second) != nullptr);
    CHECK(again.library()->size() == mm::plugin::embeddedGrooves().size() + 2);
    CHECK(again.library()->templates().front().id == mm::plugin::embeddedGrooves().templates().front().id);
}

TEST_CASE("a snapshot of the library stays as it was when a template is added", "[group-wiring][groove][own]") {
    OwnGrooves own;
    const auto snapshot = own.store.library();
    const auto size = snapshot->size();
    mm::core::GrooveTemplate groove;
    groove.nameDe = "Neu";
    groove.offsetTicks.assign(16, 0);
    groove.offsetTicks[3] = -20;
    groove.velocityPermille.assign(16, 1000);
    REQUIRE_FALSE(own.store.add(groove).empty());
    CHECK(snapshot->size() == size);
    CHECK(own.store.library()->size() == size + 1);
    CHECK(own.store.revision() > 0);
}

TEST_CASE("a voice's groove is saved as a template and plays the same", "[group-wiring][groove][own]") {
    OwnGrooves own;
    GrooveRig rig;
    // straight: nothing to save
    CHECK_FALSE(rig.hub.processor.playingVoice().canSaveGroove);
    CHECK(rig.hub.processor.saveVoiceGrooveAsTemplate(0, 0, "Nichts").empty());
    // a template with a swing, and the swing control on top of another one
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("laid_back"), 0.6f));
    const auto before = playedNotes(rig);
    CHECK(rig.hub.processor.playingVoice().canSaveGroove);
    const auto id = rig.hub.processor.saveVoiceGrooveAsTemplate(0, 0, "Mein Feel");
    REQUIRE(id == "user_mein_feel");
    CHECK(rig.track().groove.templateId == id);
    CHECK(own.store.library()->find(id) != nullptr);
    CHECK(playedNotes(rig) == before); // the same sound
    CHECK(rig.hub.processor.playingVoice().grooveId == id);
    CHECK_FALSE(rig.hub.processor.playingVoice().canSaveGroove); // it has swing of its own now: saved already
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("swing_62"), std::nullopt));
    CHECK_FALSE(rig.hub.processor.playingVoice().canSaveGroove); // a template of the library
    REQUIRE(rig.hub.processor.undo());
    // the choice is an undo step
    REQUIRE(rig.hub.processor.undo());
    CHECK(rig.track().groove.templateId == "laid_back");
    CHECK(playedNotes(rig) == before);
    // what cannot be saved
    CHECK(rig.hub.processor.saveVoiceGrooveAsTemplate(0, 0, "").empty());
    CHECK(rig.hub.processor.saveVoiceGrooveAsTemplate(0, 9, "x").empty());
    CHECK(rig.hub.processor.saveVoiceGrooveAsTemplate(5, 0, "x").empty());
}

TEST_CASE("a voice instance saves no template", "[group-wiring][groove][own]") {
    OwnGrooves own;
    GrooveRig rig;
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::string("shuffle"), std::nullopt));
    Instance voice("Voice");
    voice.setRole(InstanceRole::Voice, SlotFollow::Hub, 1);
    REQUIRE(voice.processor.slotsSnapshot().slot(0)->pattern.has_value()); // it shows the pattern of the hub
    const auto size = own.store.library()->size();
    CHECK(voice.processor.saveVoiceGrooveAsTemplate(0, 0, "Nein").empty());
    CHECK(own.store.library()->size() == size);
}

TEST_CASE("the groove box offers saving and lists the own templates", "[group-wiring][groove][own][editor]") {
    OwnGrooves own;
    GrooveRig rig;
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.hub.processor.createEditor());
    pumpMessages(80);
    auto* box = child<juce::ComboBox>(*editor, "groove_1");
    REQUIRE(box != nullptr);
    REQUIRE(waitFor([&] { return box->getNumItems() > 1 && box->isEnabled(); }));
    CHECK(box->indexOfItemId(3) < 0); // straight: nothing to save
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.6f));
    REQUIRE(waitFor([&] { return box->indexOfItemId(3) >= 0; }));
    CHECK(box->getItemText(box->indexOfItemId(3)) == "Als Vorlage speichern …");
    // choosing it asks for a name and leaves the box where it was
    box->setSelectedId(3, juce::sendNotificationSync);
    CHECK(box->getSelectedId() == 1);
    CHECK(rig.track().groove.templateId.empty());
    // the row saves under the name that was given
    auto* row = [&]() -> mm::plugin::VoiceRow* {
        for (auto* component : editor->getChildren()) {
            if (auto* found = dynamic_cast<mm::plugin::VoiceRow*>(component)) {
                return found;
            }
        }
        return nullptr;
    }();
    if (row == nullptr) {
        for (auto* component : editor->getChildren()) {
            for (auto* inner : component->getChildren()) {
                if (auto* found = dynamic_cast<mm::plugin::VoiceRow*>(inner)) {
                    row = found;
                }
            }
        }
    }
    REQUIRE(row != nullptr);
    CHECK_FALSE(row->saveGrooveAs("   "));
    REQUIRE(row->saveGrooveAs("Meins"));
    CHECK(rig.track().groove.templateId == "user_meins");
    CHECK(waitFor([&] {
        const auto index = box->indexOfItemId(box->getSelectedId());
        return index >= 0 && box->getItemText(index) == "Meins";
    }));
    CHECK(box->getNumItems() == 1 + 9); // straight and nine templates; the swing of the new one is its own: no save item
    // the dialog of the choice is closed again so that no window outlives the test
    for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) {
        if (auto* window = dynamic_cast<juce::AlertWindow*>(juce::Desktop::getInstance().getComponent(i))) {
            window->exitModalState(0);
        }
    }
    pumpMessages(20);
}

TEST_CASE("the store of own templates takes names up to the limit and a bounded number", "[group-wiring][groove][own]") {
    OwnGrooves own;
    mm::core::GrooveTemplate groove;
    groove.offsetTicks.assign(16, 0);
    groove.offsetTicks[1] = 10;
    groove.velocityPermille.assign(16, 1000);
    groove.nameDe = std::string(mm::plugin::UserGrooveStore::kMaxNameLength, 'a'); // exactly the limit
    CHECK_FALSE(own.store.add(groove).empty());
    for (size_t i = 1; i < mm::plugin::UserGrooveStore::kMaxTemplates; ++i) {
        groove.nameDe = "Groove " + std::to_string(i);
        REQUIRE_FALSE(own.store.add(groove).empty());
    }
    CHECK(own.store.library()->size() == mm::plugin::embeddedGrooves().size() + mm::plugin::UserGrooveStore::kMaxTemplates);
    groove.nameDe = "Zu viel";
    CHECK(own.store.add(groove).empty());
}

// --- the rating box of the hub (SPEC 3.12, D-246) ---------------------------------------------------------------------

TEST_CASE("the hub rates the pattern of the slot with one click", "[group-wiring][editor][rating]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::LibraryStore library{juce::File()};
    auto* previous = &mm::plugin::libraryStore();
    mm::plugin::overrideLibraryStore(&library);
    mm::plugin::ReferenceStore references{juce::File(), mm::plugin::embeddedStyles()};
    auto* previousReferences = &mm::plugin::referenceStore();
    mm::plugin::overrideReferenceStore(&references);
    struct Restore {
        mm::plugin::LibraryStore* library;
        mm::plugin::ReferenceStore* references;
        ~Restore() {
            mm::plugin::overrideLibraryStore(library);
            mm::plugin::overrideReferenceStore(references);
        }
    } restore{previous, previousReferences};

    GrooveRig rig;
    std::unique_ptr<juce::AudioProcessorEditor> editor(rig.hub.processor.createEditor());
    pumpMessages(80);
    auto* box = child<juce::ComboBox>(*editor, "rating");
    REQUIRE(box != nullptr);
    REQUIRE(waitFor([&] { return box->isVisible() && box->isEnabled(); }));
    CHECK(box->getSelectedId() == 1);
    CHECK(box->getItemText(0) == "ohne Bewertung");
    CHECK(editor->getLocalBounds().contains(box->getBounds()));
    // the click saves the pattern and rates it
    box->setSelectedId(5, juce::sendNotificationSync); // four stars
    REQUIRE(library.list().size() == 1);
    CHECK(library.list()[0].rating == 4);
    CHECK(rig.hub.processor.slotRating(0) == 4);
    // a rating made elsewhere shows up
    CHECK(rig.hub.processor.rateSlot(0, 2));
    CHECK(waitFor([&] { return box->getSelectedId() == 3; }));
    CHECK(library.list().size() == 1);
    // an edit makes a new pattern: the box shows no rating
    REQUIRE(rig.hub.processor.setVoiceGroove(0, 0, std::nullopt, 0.6f));
    CHECK(waitFor([&] { return box->getSelectedId() == 1; }));
    for (auto* other : editor->getChildren()) {
        if (other != box && other->isVisible() && other->getWidth() > 0 &&
            dynamic_cast<mm::plugin::ScaleCorner*>(other) == nullptr) {
            INFO(other->getComponentID().toStdString());
            CHECK_FALSE(box->getBounds().intersects(other->getBounds()));
        }
    }
    // an empty slot has nothing to rate
    rig.hub.setSlotParameter(2);
    CHECK(waitFor([&] { return !box->isEnabled(); }));
}
