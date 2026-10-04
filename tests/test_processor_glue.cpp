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
