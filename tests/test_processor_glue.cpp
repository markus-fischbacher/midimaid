#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <memory>
#include <set>
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
