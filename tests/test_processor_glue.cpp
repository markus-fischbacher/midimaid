#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <cmath>
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
