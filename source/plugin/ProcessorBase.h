#pragma once

#include "engine/PatternHandover.h"
#include "engine/PatternPlayer.h"
#include "plugin/MidiExporter.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Shared base of both plugin variants (instrument and MIDI-FX). Phase 0 behaviour: silent
/// audio and a hard-coded one-bar pattern played in sync with the host transport.
class ProcessorBase : public juce::AudioProcessor {
public:
    ProcessorBase(const BusesProperties& buses, juce::String name);
    ~ProcessorBase() override;

    const juce::String getName() const override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    bool hasEditor() const override;
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    /// Export for drag & drop (SPEC 3.4).
    MidiExporter& midiExporter();
    /// Last tempo reported by the host (120 until the host reported one). Safe to read from any thread.
    double lastKnownBpm() const;

    /// Message thread: queues a pattern for the audio thread (SPEC 6.3). A switch takes effect at the next grid point
    /// (`gridPpq`, rounded up to a bar line), an edit at the next block without restarting the position.
    void switchPattern(std::unique_ptr<mm::engine::OwnedPattern> pattern, double gridPpq = 4.0);
    void editPattern(std::unique_ptr<mm::engine::OwnedPattern> pattern);
    /// Message thread: result for `slot` (1 to 16); it plays at the next grid point once that slot is selected.
    void submitResult(int slot, std::unique_ptr<mm::engine::OwnedPattern> pattern, double gridPpq = 4.0);
    /// Message thread: the slot parameter (1 to 16). Stand-in until the host parameter `slot` exists; the audio
    /// thread reads it at each block start (SPEC 3.13).
    void selectSlot(int slot);
    /// Slot that is selected (1 to 16). Safe to read from any thread.
    int activeSlot() const;
    /// Version of the pattern that is playing (0 for the built-in placeholder). Safe to read from any thread.
    uint64_t activePatternVersion() const;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    void writeEvents(juce::MidiBuffer& midi) const;

    /// Empties the handover's return queue on the message thread (SPEC 6.3: patterns are freed there only).
    class ReturnCollector : private juce::Timer {
    public:
        explicit ReturnCollector(mm::engine::PatternHandover& handover);
        ~ReturnCollector() override;

    private:
        void timerCallback() override;
        mm::engine::PatternHandover& handover_;
    };

    juce::String name_;
    mm::engine::PatternHandover handover_;
    mm::engine::PatternPlayer player_;
    ReturnCollector returnCollector_;
    mm::engine::MidiEventList events_;
    std::atomic<double> lastBpm_{120.0};
    std::atomic<int> requestedSlot_{1};
    MidiExporter exporter_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProcessorBase)
};

/// Instrument variant (VST3, AU `aumu`, Standalone): silent stereo output plus MIDI in and out.
class InstrumentProcessor : public ProcessorBase {
public:
    explicit InstrumentProcessor(juce::String name);

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
};

/// MIDI-FX variant (AU `aumi`): no audio buses, MIDI in and out.
class MidiFxProcessor : public ProcessorBase {
public:
    explicit MidiFxProcessor(juce::String name);

    bool isMidiEffect() const override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
};

} // namespace mm::plugin
