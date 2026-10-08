#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {

class FakePlayHead : public juce::AudioPlayHead {
public:
    juce::Optional<PositionInfo> getPosition() const override {
        if (!available) {
            return {};
        }
        return info;
    }

    PositionInfo info;
    bool available = true;
};

struct NoteEvent {
    long long sample;
    bool on;
};

/// Runs `blocks` blocks of `blockSize` samples at 120 BPM from PPQ 0 through the processor.
std::vector<NoteEvent> run(juce::AudioProcessor& processor, FakePlayHead& head, int blocks, int blockSize,
                           int numChannels) {
    constexpr double kSampleRate = 48000.0;
    processor.setRateAndBufferSizeDetails(kSampleRate, blockSize);
    processor.setPlayHead(&head);
    processor.prepareToPlay(kSampleRate, blockSize);

    std::vector<NoteEvent> events;
    juce::AudioBuffer<float> audio(numChannels, blockSize);
    juce::MidiBuffer midi;
    long long clock = 0;
    for (int block = 0; block < blocks; ++block) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(clock) / kSampleRate * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            events.push_back({clock + metadata.samplePosition, message.isNoteOn()});
        }
        clock += blockSize;
    }
    return events;
}

} // namespace

TEST_CASE("instrument processor plays the placeholder pattern from the host playhead", "[plugin]") {
    mm::plugin::InstrumentProcessor processor("Test");
    FakePlayHead head;
    const auto events = run(processor, head, 100, 512, 2); // about 1.07 s = 2.13 beats

    REQUIRE(events.size() >= 4);
    CHECK(events[0].on);
    CHECK(std::abs(events[0].sample - 12000) <= 1); // PPQ 0.5
    CHECK_FALSE(events[1].on);                      // note ends 0.375 PPQ later
    CHECK(std::abs(events[1].sample - 21000) <= 1); // PPQ 0.875
    CHECK(std::abs(events[2].sample - 36000) <= 1); // PPQ 1.5
}

TEST_CASE("MIDI-FX processor without audio buses plays too", "[plugin]") {
    mm::plugin::MidiFxProcessor processor("Test");
    FakePlayHead head;
    const auto events = run(processor, head, 100, 512, 0);
    REQUIRE(events.size() >= 2);
    CHECK(events[0].on);
}

TEST_CASE("no playhead position: nothing is played", "[plugin]") {
    mm::plugin::InstrumentProcessor processor("Test");
    FakePlayHead head;
    head.available = false;
    CHECK(run(processor, head, 100, 512, 2).empty());
}

TEST_CASE("bypass releases a sounding note", "[plugin]") {
    mm::plugin::InstrumentProcessor processor("Test");
    FakePlayHead head;
    // 30 blocks of 512 = 15360 samples: the first note (from sample 12000) is sounding.
    auto events = run(processor, head, 30, 512, 2);
    REQUIRE(!events.empty());
    REQUIRE(events.back().on);

    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlockBypassed(audio, midi);
    int noteOffs = 0;
    for (const auto metadata : midi) {
        noteOffs += metadata.getMessage().isNoteOff() ? 1 : 0;
    }
    CHECK(noteOffs == 1);
}

namespace {

bool waitForExport(mm::plugin::MidiExporter& exporter, double bpm) {
    for (int i = 0; i < 400; ++i) {
        if (exporter.readyFile() != juce::File() && std::abs(exporter.exportedBpm() - bpm) < 0.01) {
            return true;
        }
        juce::Thread::sleep(5);
    }
    return false;
}

} // namespace

TEST_CASE("exported .mid is readable by JUCE and carries the DAW tempo", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    processor.midiExporter().requestExport(128.0);
    REQUIRE(waitForExport(processor.midiExporter(), 128.0));

    const auto file = processor.midiExporter().readyFile();
    CHECK(file.getFileName() == "MidiMaid_Slot1_Bass.mid");

    juce::MidiFile midi;
    juce::FileInputStream stream(file);
    REQUIRE(stream.openedOk());
    REQUIRE(midi.readFrom(stream));
    CHECK(midi.getNumTracks() == 1);
    CHECK(midi.getTimeFormat() == 960);

    const auto* track = midi.getTrack(0);
    int noteOns = 0;
    int noteOffs = 0;
    double secondsPerQuarter = 0.0;
    for (const auto* event : *track) {
        noteOns += event->message.isNoteOn() ? 1 : 0;
        noteOffs += event->message.isNoteOff() ? 1 : 0;
        if (event->message.isTempoMetaEvent()) {
            secondsPerQuarter = event->message.getTempoSecondsPerQuarterNote();
        }
    }
    CHECK(noteOns == 4);
    CHECK(noteOffs == 4);
    CHECK(std::abs(60.0 / secondsPerQuarter - 128.0) < 0.01);
}

TEST_CASE("exported file is removed with the instance", "[plugin][export]") {
    juce::File file;
    {
        mm::plugin::InstrumentProcessor processor("Test");
        processor.midiExporter().requestExport(120.0);
        REQUIRE(waitForExport(processor.midiExporter(), 120.0));
        file = processor.midiExporter().readyFile();
        REQUIRE(file.existsAsFile());
    }
    CHECK_FALSE(file.exists());
    CHECK_FALSE(file.getParentDirectory().exists());
}

TEST_CASE("editor starts the export and can be created and destroyed", "[plugin][export]") {
    juce::ScopedJuceInitialiser_GUI gui;
    mm::plugin::InstrumentProcessor processor("Test");
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        REQUIRE(editor != nullptr);
        CHECK(waitForExport(processor.midiExporter(), processor.lastKnownBpm()));
    }
}

TEST_CASE("last known tempo follows the host", "[plugin][export]") {
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(processor.lastKnownBpm() == 120.0);
    FakePlayHead head;
    run(processor, head, 2, 512, 2); // runs at 120 BPM
    head.info.setBpm(140.0);
    head.info.setPpqPosition(0.0);
    head.info.setIsPlaying(true);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    processor.processBlock(audio, midi);
    CHECK(processor.lastKnownBpm() == 140.0);
}

namespace {

/// A processor driven like a host would: 512-sample blocks at 120 BPM, one persistent MIDI buffer.
class Rig {
public:
    explicit Rig(mm::plugin::ProcessorBase& processor) : processor_(processor), audio_(2, kBlock) {
        processor.setRateAndBufferSizeDetails(kSampleRate, kBlock);
        processor.setPlayHead(&head_);
        processor.prepareToPlay(kSampleRate, kBlock);
    }

    struct Note {
        long long sample;
        bool on;
        int pitch;
    };

    std::vector<Note> run(int blocks) {
        std::vector<Note> notes;
        for (int i = 0; i < blocks; ++i) {
            head_.info.setIsPlaying(true);
            head_.info.setBpm(120.0);
            head_.info.setPpqPosition(static_cast<double>(clock_) / kSampleRate * 2.0);
            audio_.clear();
            midi_.clear();
            processor_.processBlock(audio_, midi_);
            for (const auto metadata : midi_) {
                const auto message = metadata.getMessage();
                notes.push_back({clock_ + metadata.samplePosition, message.isNoteOn(), message.getNoteNumber()});
            }
            clock_ += kBlock;
        }
        return notes;
    }

    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlock = 512;

private:
    mm::plugin::ProcessorBase& processor_;
    FakePlayHead head_;
    juce::AudioBuffer<float> audio_;
    juce::MidiBuffer midi_;
    long long clock_ = 0;
};

std::unique_ptr<mm::engine::OwnedPattern> ownedPattern(std::vector<mm::core::PatternNote> notes, uint64_t version,
                                                       std::shared_ptr<const void> token = {}) {
    auto pattern = std::make_unique<mm::engine::OwnedPattern>();
    pattern->notes = std::move(notes);
    pattern->lengthTicks = 4 * mm::core::kTicksPerQuarter;
    pattern->version = version;
    pattern->lifetimeToken = std::move(token);
    return pattern;
}

} // namespace

TEST_CASE("a switch queued on the processor takes effect at the next bar line", "[plugin][handover]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    CHECK(processor.activePatternVersion() == 0);
    rig.run(10);
    processor.switchPattern(ownedPattern({{0, 240, 1, 50, 100}}, 5));
    const auto notes = rig.run(200); // to about 4.3 PPQ
    std::vector<Rig::Note> fifties;
    for (const auto& n : notes) {
        if (n.pitch == 50 && n.on) {
            fifties.push_back(n);
        }
    }
    REQUIRE(fifties.size() == 1);
    CHECK(std::abs(fifties.front().sample - 96000) <= 1); // PPQ 4.0
    CHECK(processor.activePatternVersion() == 5);
    for (const auto& n : notes) {
        CHECK((n.pitch == 50 || n.sample < 96000)); // the placeholder stops at the bar line
    }
}

TEST_CASE("an edit queued on the processor keeps the position", "[plugin][handover]") {
    mm::plugin::MidiFxProcessor processor("Test");
    Rig rig(processor);
    rig.run(10);
    processor.editPattern(ownedPattern({{1920, 240, 1, 50, 100}}, 6)); // position 2.0
    const auto notes = rig.run(120);                                   // to about 2.6 PPQ
    std::vector<Rig::Note> fifties;
    for (const auto& n : notes) {
        CHECK((n.pitch == 50 || n.sample < 5120 + 512)); // no placeholder note after the edit
        if (n.pitch == 50 && n.on) {
            fifties.push_back(n);
        }
    }
    REQUIRE(fifties.size() == 1);
    CHECK(std::abs(fifties.front().sample - 48000) <= 1); // PPQ 2.0: no restart
    CHECK(processor.activePatternVersion() == 6);
}

TEST_CASE("destroying a processor frees active and pending patterns", "[plugin][handover]") {
    auto active = std::make_shared<int>(0);
    auto pending = std::make_shared<int>(0);
    auto replaced = std::make_shared<int>(0);
    {
        mm::plugin::InstrumentProcessor processor("Test");
        Rig rig(processor);
        processor.editPattern(ownedPattern({{0, 240, 1, 50, 100}}, 1, replaced));
        rig.run(2);
        processor.editPattern(ownedPattern({{0, 240, 1, 51, 100}}, 2, active)); // retires the first one
        rig.run(2);
        processor.switchPattern(ownedPattern({{0, 240, 1, 52, 100}}, 3, pending)); // never taken
        CHECK(active.use_count() == 2);
        CHECK(pending.use_count() == 2);
    }
    CHECK(active.use_count() == 1);
    CHECK(pending.use_count() == 1);
    CHECK(replaced.use_count() == 1);
}

TEST_CASE("the host MIDI buffer keeps its storage across blocks", "[plugin][handover]") {
    // The buffer belongs to the host (O-23, D-134): the processor reserves the bound once. JUCE allocates the storage
    // with malloc, which cannot be counted portably, so this only guards that the storage does not move between
    // blocks (the address of the first event).
    mm::plugin::InstrumentProcessor processor("Test");
    FakePlayHead head;
    processor.setRateAndBufferSizeDetails(48000.0, 512);
    processor.setPlayHead(&head);
    processor.prepareToPlay(48000.0, 512);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    const juce::uint8* storage = nullptr;
    int blocksWithEvents = 0;
    for (int block = 0; block < 400; ++block) {
        head.info.setIsPlaying(true);
        head.info.setBpm(120.0);
        head.info.setPpqPosition(static_cast<double>(block) * 512.0 / 48000.0 * 2.0);
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        for (const auto metadata : midi) {
            if (storage == nullptr) {
                storage = metadata.data;
            }
            CHECK(metadata.data - storage < 12288); // inside the one reserved block
            ++blocksWithEvents;
            break;
        }
    }
    CHECK(blocksWithEvents >= 6);
}

TEST_CASE("selecting a slot on the processor plays its pattern from the next bar line", "[plugin][handover]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    processor.submitResult(2, ownedPattern({{0, 240, 1, 55, 100}}, 4)); // stored, slot 1 keeps playing
    rig.run(10);
    CHECK(processor.activePatternVersion() == 0);
    processor.selectSlot(2);
    CHECK(processor.activeSlot() == 2);
    const auto notes = rig.run(200); // to about 4.3 PPQ
    std::vector<Rig::Note> played;
    for (const auto& n : notes) {
        if (n.pitch == 55 && n.on) {
            played.push_back(n);
        }
    }
    REQUIRE(played.size() == 1);
    CHECK(std::abs(played.front().sample - 96000) <= 1); // PPQ 4.0
    CHECK(processor.activePatternVersion() == 4);
}

// ---- host parameters (SPEC 3.13, D-93, D-138) ----

namespace {

void setParameter(mm::plugin::ProcessorBase& processor, const char* id, float unnormalised) {
    auto* parameter = processor.parameters().getParameter(id);
    REQUIRE(parameter != nullptr);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(unnormalised));
}

} // namespace

TEST_CASE("every register parameter exists with its ID, range, default and automation flag", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    CHECK(static_cast<size_t>(processor.getParameters().size()) == mm::core::kParameterCount);
    for (const auto& spec : mm::core::parameterRegister()) {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(
            processor.parameters().getParameter(juce::String(spec.id.data(), spec.id.size())));
        REQUIRE(parameter != nullptr);
        INFO(std::string(spec.id));
        const auto range = parameter->getNormalisableRange();
        CHECK(static_cast<int>(range.start) == spec.min);
        CHECK(static_cast<int>(range.end) == spec.max);
        CHECK(static_cast<int>(std::lround(parameter->convertFrom0to1(parameter->getDefaultValue()))) ==
              spec.defaultValue);
        CHECK(parameter->getVersionHint() == mm::core::kParameterVersionHint);
        CHECK(parameter->isAutomatable() == spec.isActive()); // later releases cannot be automated
        CHECK(parameter->getName(64) == juce::String(spec.name.data(), spec.name.size()));
    }
}

TEST_CASE("the slot parameter drives the playback and a start plays it at once", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    processor.submitResult(3, ownedPattern({{0, 240, 1, 57, 100}}, 8));
    setParameter(processor, "slot", 3.0f);
    CHECK(processor.activeSlot() == 3);
    const auto notes = rig.run(10); // the first block is a start
    REQUIRE_FALSE(notes.empty());
    CHECK(notes.front().pitch == 57);
    CHECK(notes.front().sample == 0);
    CHECK(processor.activePatternVersion() == 8);
}

TEST_CASE("the mute parameter silences the voice and un-muting brings it back", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    auto notes = rig.run(30); // the first note (from sample 12000) sounds
    REQUIRE_FALSE(notes.empty());
    REQUIRE(notes.back().on);

    setParameter(processor, "mute_1", 1.0f);
    notes = rig.run(2);
    int offs = 0;
    for (const auto& n : notes) {
        offs += n.on ? 0 : 1;
    }
    CHECK(offs == 1);
    notes = rig.run(120); // more notes would start
    CHECK(notes.empty());

    setParameter(processor, "mute_1", 0.0f);
    notes = rig.run(200);
    CHECK_FALSE(notes.empty());
}

TEST_CASE("the mute of another voice does not silence this instance", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    Rig rig(processor);
    setParameter(processor, "mute_2", 1.0f);
    CHECK_FALSE(rig.run(100).empty());
}

TEST_CASE("the state keeps the parameter values and is stable", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor source("Test");
    setParameter(source, "slot", 5.0f);
    setParameter(source, "mute_1", 1.0f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    CHECK(target.activeSlot() == 5);
    CHECK(target.parameters().getRawParameterValue("mute_1")->load() >= 0.5f);

    juce::MemoryBlock again;
    target.getStateInformation(again);
    CHECK(again == saved); // saving what was loaded gives the same bytes
}

TEST_CASE("damaged or foreign state data is ignored", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor processor("Test");
    setParameter(processor, "slot", 4.0f);

    const char garbage[] = "this is not a state";
    processor.setStateInformation(garbage, static_cast<int>(sizeof(garbage)));
    processor.setStateInformation(nullptr, 0);
    CHECK(processor.activeSlot() == 4);

    juce::XmlElement foreign("SomethingElse");
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary(foreign, block);
    processor.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
    CHECK(processor.activeSlot() == 4);

    // A valid state without a version number is not trusted.
    mm::plugin::InstrumentProcessor other("Test");
    setParameter(other, "slot", 12.0f);
    juce::MemoryBlock valid;
    other.getStateInformation(valid);
    auto unversioned = juce::AudioProcessor::getXmlFromBinary(valid.getData(), static_cast<int>(valid.getSize()));
    REQUIRE(unversioned != nullptr);
    unversioned->removeAttribute("stateVersion");
    juce::MemoryBlock block2;
    juce::AudioProcessor::copyXmlToBinary(*unversioned, block2);
    processor.setStateInformation(block2.getData(), static_cast<int>(block2.getSize()));
    CHECK(processor.activeSlot() == 4);
}

TEST_CASE("a newer state version loads as far as understood, unknown parameters are skipped", "[plugin][parameters]") {
    mm::plugin::InstrumentProcessor source("Test");
    setParameter(source, "slot", 9.0f);
    juce::MemoryBlock saved;
    source.getStateInformation(saved);

    auto root = juce::AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(root != nullptr);
    root->setAttribute("stateVersion", 99);
    root->setAttribute("somethingNew", "x");
    if (auto* parameters = root->getChildElement(0)) {
        auto* unknown = parameters->createNewChildElement("PARAM");
        unknown->setAttribute("id", "no_such_parameter");
        unknown->setAttribute("value", 3.0);
    }
    root->createNewChildElement("FutureSection");
    juce::MemoryBlock newer;
    juce::AudioProcessor::copyXmlToBinary(*root, newer);

    mm::plugin::InstrumentProcessor target("Test");
    target.setStateInformation(newer.getData(), static_cast<int>(newer.getSize()));
    CHECK(target.activeSlot() == 9);
}
