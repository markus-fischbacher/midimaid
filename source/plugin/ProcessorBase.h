#pragma once

#include "core/InstanceSettings.h"
#include "core/ParameterRegister.h"
#include "core/SlotBank.h"
#include "core/StyleLibrary.h"
#include "engine/GroupChannel.h"
#include "engine/GroupSync.h"
#include "engine/PatternHandover.h"
#include "engine/PatternPlayer.h"
#include "engine/SlotPublisher.h"
#include "plugin/GenerationService.h"
#include "plugin/GroupLink.h"
#include "plugin/MidiExporter.h"

#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

namespace mm::plugin {

/// What the last "Generate" did, for the editor (message thread).
enum class GenerationStatus { Idle, Generating, Done, NoResult, UseHub };

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
    /// Message thread: sets the host parameter `slot` (1 to 16) as if the user had moved it. The audio thread reads
    /// the parameter at each block start and never writes it (SPEC 3.13).
    void selectSlot(int slot);
    /// Value of the host parameter `slot` (1 to 16). Safe to read from any thread.
    int activeSlot() const;
    /// The host parameters of SPEC 3.13, created from the register (D-93).
    juce::AudioProcessorValueTreeState& parameters();
    /// The styles of this plugin (embedded profiles, D-139).
    const mm::core::StyleLibrary& styles() const { return styles_; }
    /// Any thread: runs `change` on the slot bank under the instance lock, then hands every changed slot to the
    /// engine (quantized to the next bar). The audio thread never touches the bank.
    void editSlots(const std::function<void(mm::core::SlotBank&)>& change);
    /// A copy of the slot bank.
    mm::core::SlotBank slotsSnapshot() const;
    /// Role, output mode and output voice (SPEC 6.5, 9.1). The voice also selects the mute parameter that applies.
    mm::core::InstanceSettings instanceSettings() const;
    void setInstanceSettings(const mm::core::InstanceSettings& settings);
    /// Message thread: generates a pattern in the background for the slot that is selected now (SPEC 3.1, D-140).
    /// The result lands in that slot and plays at the next bar line; during an offline bounce it waits until the
    /// host is real-time again. A new request replaces a running one.
    void generate();
    GenerationStatus generationStatus() const { return generationStatus_; }
    /// Group (SPEC 6.5, D-141): where this instance stands, how many voices a hub has, and the offer to take over a
    /// lost hub. Safe to read from any thread; `acceptHubOffer` is for the message thread.
    mm::core::GroupStatus groupStatus() const;
    size_t groupVoices() const;
    void acceptHubOffer();
    /// Called by the group link on the message thread when the group status changes: takes or gives back the slot in
    /// the group channel and recomputes the slot mode.
    void onGroupStatusChanged(mm::core::GroupStatus status);
    /// Planned slot changes that reached this voice too late for their point (SPEC 6.5, D-142).
    uint32_t lateSwitches() const { return groupSync_.lateSwitches(); }
    /// Called by the group link on the message thread: a voice takes over the hub's slot set. Other roles ignore it.
    void adoptHubSlots(const mm::core::SlotSnapshot& snapshot);
    /// Slots that were left empty by the last state load because their data was bad (path and reason).
    std::vector<std::string> slotLoadProblems() const;
    /// Version of the pattern that is playing (0 for the built-in placeholder). Safe to read from any thread.
    uint64_t activePatternVersion() const;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    void writeEvents(juce::MidiBuffer& midi) const;
    void onGenerated(const GenerationJob& job, std::optional<mm::core::Pattern> pattern);
    void applyGenerated(const GenerationJob& job, mm::core::Pattern pattern);
    void setInstanceSettingsLocked(const mm::core::InstanceSettings& settings);
    void updateSlotMode(); // message thread

    /// Empties the handover's return queue on the message thread (SPEC 6.3: patterns are freed there only).
    class ReturnCollector : private juce::Timer {
    public:
        explicit ReturnCollector(mm::engine::PatternHandover& handover);
        ~ReturnCollector() override;

    private:
        void timerCallback() override;
        mm::engine::PatternHandover& handover_;
    };

    /// Version of the plugin state (SPEC 9.1); every change of its format raises it and gets a migration.
    static constexpr int kStateVersion = 1;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::String name_;
    juce::AudioProcessorValueTreeState parameters_;
    std::atomic<float>* slotValue_ = nullptr;                            // raw values, read in the audio thread
    std::array<std::atomic<float>*, mm::core::kMaxVoices> muteValues_{}; // raw values, read in the audio thread
    std::atomic<int> outputVoice_{1};                                    // the voice this instance plays (1 = first)
    const mm::core::StyleLibrary& styles_;
    mutable juce::CriticalSection slotsLock_; // message and state threads only, never the audio thread
    mm::core::SlotBank slots_;
    mm::core::InstanceSettings settings_;
    std::vector<std::string> slotLoadProblems_;
    mm::engine::PatternHandover handover_;
    mm::engine::SlotPublisher publisher_;
    GenerationStatus generationStatus_ = GenerationStatus::Idle; // message thread
    struct ParkedResult {
        GenerationJob job;
        mm::core::Pattern pattern;
    };
    std::optional<ParkedResult> parked_; // a result that arrived during an offline render (message thread)
    class ParkTimer;
    std::unique_ptr<ParkTimer> parkTimer_;
    mm::engine::PatternPlayer player_;
    ReturnCollector returnCollector_;
    mm::engine::MidiEventList events_;
    std::atomic<double> lastBpm_{120.0};
    MidiExporter exporter_;

    mm::engine::GroupSync groupSync_{mm::engine::processGroupChannel(), mm::engine::GroupChannel::kNone};
    std::atomic<int> slotMode_{static_cast<int>(mm::engine::SlotMode::Own)}; // read in the audio thread
    int channelMember_ = mm::engine::GroupChannel::kNone;                    // message thread
    std::shared_ptr<GroupLink> groupLink_;
    GenerationService generator_; // last: destroyed first, so no delivery reaches a half-destroyed instance

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
