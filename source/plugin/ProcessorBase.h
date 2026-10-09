#pragma once

#include "core/InstanceSettings.h"
#include "core/ParameterRegister.h"
#include "core/RollEdit.h"
#include "core/SlotBank.h"
#include "core/StyleLibrary.h"
#include "core/UndoStack.h"
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
/// `DoneLocked`: done, and locked voices, key and length of the slot were kept. `AllLocked`: every voice is locked,
/// nothing was generated.
enum class GenerationStatus {
    Idle,
    Generating,
    Done,
    DoneLocked,
    NoResult,
    UseHub,
    Forwarded,
    AllLocked,
    Varied,
    NothingToVary,
    VaryAllLocked
};

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
    /// What this instance plays now, for the editor's roll and the export: the pattern of the playing slot (the slot of
    /// the audio thread, before the first block the slot parameter) rendered for its voice with the octave. Message
    /// thread.
    struct VoiceView {
        bool hasPattern = false;
        std::vector<mm::core::PatternNote> notes;
        uint32_t lengthTicks = 0;
        int slot = 1; ///< 1 to 16
        int voice = 1;
        int octave = 0;
        uint64_t origin = 0;      ///< of the slot bank
        uint64_t revision = 0;    ///< of the slot
        juce::String voiceName;   ///< Bass, Melody or Voice n: the names of the export files, never translated
        juce::String displayName; ///< the same in the language of the UI
        bool melody = false;      ///< the voice has the melody role (accent colour)
        bool locked = false;      ///< the voice is locked (SPEC 3.5)
        /// What the piano roll edits: the source notes of the pattern (with ids, no groove, no instance octave).
        std::vector<mm::core::RollNote> source;
        mm::core::PitchClass root = 9;
        std::string scaleId;
        /// True when `other` shows the same thing (no need to redraw or export again).
        bool sameSource(const VoiceView& other) const {
            return hasPattern == other.hasPattern && slot == other.slot && voice == other.voice &&
                   octave == other.octave && origin == other.origin && revision == other.revision;
        }
    };
    VoiceView playingVoice() const;
    /// Every voice of the pattern in the playing slot, in pattern order (iterates `Pattern::voices`, D-52); empty when
    /// the slot holds no pattern. The octave of this instance applies to its own voice only. Message thread.
    std::vector<VoiceView> playingVoices() const;
    /// Which of the 16 slots hold a pattern. Message thread.
    std::array<bool, mm::core::kSlotCount> slotUsage() const;
    /// What the playing slot holds, for the info line of the hub UI. Message thread.
    struct SlotInfo {
        bool filled = false;
        int slot = 1; ///< 1 to 16
        std::string styleId;
        int root = 0;
        std::string scaleId;
        uint32_t lengthBars = 0;
        uint64_t seed = 0;
        uint64_t winnerSeed = 0;
        bool operator==(const SlotInfo&) const = default;
    };
    SlotInfo playingSlotInfo() const;
    /// True when this instance is silent because of a mute switch: its own, or (for a voice) the hub's. Any thread.
    bool mutedByOwnSwitch() const;
    bool mutedByHub() const;
    /// Voice: asks the hub to bring its window to the front, as far as the host allows (SPEC 8.1).
    void showHub();
    /// Message thread: brings the window of this instance's editor to the front, if there is one.
    void bringEditorToFront();
    /// How often this instance was asked to bring its window to the front (for tests and diagnosis).
    uint32_t frontRequests() const { return frontRequests_.load(); }
    /// Message thread: makes the exported file match what this instance plays now (the playing slot's pattern for its
    /// voice, with the octave, at the DAW tempo); writes in the background and only when something changed. An empty
    /// slot leaves nothing to drag. Called by the editor's timer.
    void updateExport();
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
    /// Message thread: edits the current pattern of the slot (0-based index, like `editSlots`) with `change`, which
    /// returns how many notes it changed (the `core/PatternEdit` functions). Nothing changes and nothing is recorded
    /// when it returns 0, the slot is empty or this instance is a voice (voices only show, SPEC 8.1). The change
    /// plays at once, without waiting for the bar line, and is an undo step; `mergeWithPrevious` continues the
    /// previous step (one mouse gesture). A hub hands the slots on to its voices, which take the change at their next
    /// bar line. True when the pattern changed.
    bool editNotes(size_t slotIndex, const std::function<size_t(mm::core::Pattern&)>& change,
                   bool mergeWithPrevious = false);
    /// Message thread: takes the last undo step back or does the undone one again (D-153). The result plays at once.
    /// False when there is nothing to do or this instance is a voice.
    bool undo();
    /// Message thread: locks or unlocks a voice of the slot (0-based indices, SPEC 3.5). An undo step like a note edit.
    /// False in a voice, for an empty slot or when the lock is already as asked.
    bool setVoiceLocked(size_t slotIndex, size_t voice, bool locked);
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
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
    /// Message thread: varies the pattern of the slot that is selected now (SPEC 3.6, D-155) with the subtle operators,
    /// synchronously (it is purely algorithmic). `voice` limits it to one voice (0-based), `seed` makes it repeatable
    /// (default: random). The result is a history entry and an undo step and plays at the next bar line. Locked
    /// voices stay as they are. False, with the status telling why, for a voice instance, an empty slot, all voices
    /// locked, strength 0 or when nothing could change.
    bool vary(int strengthPct, std::optional<size_t> voice = std::nullopt, std::optional<uint64_t> seed = std::nullopt);
    /// How many notes the last successful `vary` changed.
    size_t lastVariationChanges() const { return lastVariationChanges_; }
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
    void adoptHubSlots(const mm::core::SlotSnapshot& snapshot, std::optional<double> stampPpq = std::nullopt);
    /// Slots that were left empty by the last state load because their data was bad (path and reason).
    std::vector<std::string> slotLoadProblems() const;
    /// Version of the pattern that is playing (0 for the built-in placeholder). Safe to read from any thread.
    uint64_t activePatternVersion() const;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    bool stepUndo(bool back);
    void writeEvents(juce::MidiBuffer& midi) const;
    // Under `slotsLock_`: the playing slot (also fills slot, origin and revision of `view`), and one voice of a
    // pattern.
    const mm::core::Slot* playingSlotLocked(VoiceView& view) const;
    void fillVoiceLocked(VoiceView& view, const mm::core::Pattern& pattern, size_t voiceIndex) const;
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
    mm::core::UndoStack undo_;                // under `slotsLock_`
    mutable juce::CriticalSection slotsLock_; // message and state threads only, never the audio thread
    mm::core::SlotBank slots_;
    mm::core::InstanceSettings settings_;
    std::vector<std::string> slotLoadProblems_;
    mm::engine::PatternHandover handover_;
    mm::engine::SlotPublisher publisher_;
    GenerationStatus generationStatus_ = GenerationStatus::Idle; // message thread
    size_t lastVariationChanges_ = 0;                            // message thread
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
    struct ExportKey {
        uint64_t origin = 0;
        uint64_t revision = 0;
        int slot = 0;
        int voice = 0;
        int octave = 0;
        long long bpmCentis = 0;
        bool all = false; ///< every voice is exported (Solo and Hub), not only the own one
        bool empty = true;
        bool operator==(const ExportKey&) const = default;
    };
    std::optional<ExportKey> exportKey_; // message thread
    std::atomic<uint32_t> frontRequests_{0};

    mm::engine::GroupSync groupSync_{mm::engine::processGroupChannel(), mm::engine::GroupChannel::kNone};
    std::atomic<int> slotMode_{static_cast<int>(mm::engine::SlotMode::Own)}; // read in the audio thread
    std::atomic<bool> silent_{false};    // a hub with output mode "none" plays nothing (audio thread reads)
    std::atomic<bool> voiceRole_{false}; // a voice also obeys the hub's mute switches (audio thread reads)
    int channelMember_ = mm::engine::GroupChannel::kNone; // message thread
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
