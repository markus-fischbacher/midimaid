#pragma once

#include "core/Library.h"
#include "core/EvolveLayer.h"
#include "core/InstanceSettings.h"
#include "core/ParameterRegister.h"
#include "core/RollEdit.h"
#include "core/Transpose.h"
#include "core/SlotBank.h"
#include "core/StyleLibrary.h"
#include "core/UndoStack.h"
#include "engine/GroupChannel.h"
#include "engine/GroupSync.h"
#include "engine/PatternHandover.h"
#include "engine/PatternPlayer.h"
#include "engine/PlayheadMeter.h"
#include "engine/SlotPublisher.h"
#include "plugin/StateMigration.h"
#include "plugin/GenerationService.h"
#include "plugin/GroupLink.h"
#include "plugin/MidiExporter.h"
#include "plugin/MidiImportService.h"

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
    HubWindowClosed, ///< "Open hub" found the window of the hub closed
    Forwarded,
    AllLocked,
    Varied,
    NothingToVary,
    VaryAllLocked,
    Renewed,
    NothingToRenew,
    RenewLocked,
    AiFailed,            ///< the AI request failed or gave nothing usable; `lastReport()` says why (SPEC 7.9)
    GenerationCancelled, ///< the musician cancelled the request
    Refined,             ///< a refinement was applied (SPEC 3.15)
    NothingToRefine,     ///< no pattern in the slot, or no instruction
    RefineAllLocked,     ///< every voice in the scope is locked
    RefineNeedsAi,       ///< refining asks the AI and none is set up
    ImportRead,          ///< a dropped MIDI file was read (SPEC 3.18); `pendingImport()` holds the plan
    ImportReadCut,       ///< the same, but the clip is longer than 16 bars: only the first 16 count
    ImportNotFourFour,   ///< the file has a time signature other than 4/4: no import
    ImportNoNotes,       ///< the file has no notes
    ImportUnreadable,    ///< the file is no usable MIDI file
    ImportApplied,       ///< the voice was imported; the other voices were not generated (another slot is selected)
    DoneImportCut,       ///< the other voices were generated around the imported one; the clip was cut to 16 bars
    DrumRefApplied,      ///< a dropped drum clip became the drum reference of the slot (SPEC 3.14, D-179)
    DrumRefStored,       ///< the same, but the slot is empty: the reference is used for the next generation
    DrumRefNoKickHat,    ///< the drum clip has neither kick nor hat (by the mapping of the settings)
    DrumRefRemoved,      ///< the drum reference was taken out
    LibrarySaved,        ///< the slot was saved in the library (SPEC 3.12, D-184)
    LibrarySetSaved,     ///< all slots were saved as a set
    LibraryNothingToSave, ///< the slot (or every slot) is empty
    LibraryFull,         ///< the library holds `kMaxLibraryEntries` entries
    LibraryLoaded,       ///< an entry was put into a slot
    LibrarySetLoaded,    ///< a set was put into the slots it holds
    LibraryLoadFailed,   ///< the entry is gone or cannot be read
    LibraryImported,     ///< a MIDI file became a library entry
    LibraryExported,     ///< an entry was written as a MIDI file
    LibraryExportFailed, ///< the entry cannot be exported (a set, no notes, unreadable, no write access)
    SlotCopied,          ///< a slot went to the clipboard of the instance (SPEC 3.10)
    SlotPasted,          ///< the clipboard became the result of a slot
    SlotNothingToCopy,   ///< the slot is empty, or nothing is on the clipboard
    SlotCleared,         ///< a slot was reset completely
    SlotsSwapped,        ///< two slots changed places
    EvolveKept,          ///< the stage that sounded went into the slot (SPEC 6.7)
    EvolveNothingToKeep  ///< no stage other than the slot sounds, or Evolve is off
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
        std::string grooveId;       ///< the groove template of the voice: "straight", "drum_reference" or a library id (SPEC 3.9)
        float swing = 0.5f;         ///< what the swing control shows (0.50 to 0.75)
        bool swingActive = true;    ///< false: the template has swing of its own and the control is off
        bool drumReference = false; ///< the pattern has a drum reference to choose
        mm::core::TransposeInfo transposeInfo; ///< key, range and extremes of the voice (live transposition, SPEC 3.16)
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
        bool drumReference = false; ///< the pattern has a drum reference (SPEC 3.14)
        size_t historySize = 0;  ///< results kept for the slot (SPEC 3.6)
        size_t historyIndex = 0; ///< 0-based position of the entry the current pattern shows or is based on
        bool operator==(const SlotInfo&) const = default;
    };
    SlotInfo playingSlotInfo() const;
    /// The host position in PPQ now, for the playhead of the piano roll (SPEC 8.1); none while the transport stands,
    /// without position data or when this instance does not process. Any thread except the audio thread.
    std::optional<double> playheadPpq() const;
    /// The clock of the playhead in seconds, a steady one (default: the system's). Tests replace it to make the age of
    /// a reading independent of the speed of the machine; none restores the default. Process-wide.
    using PlayheadClock = double (*)();
    static void setPlayheadClock(PlayheadClock clock);
    /// True when this instance is silent because of a mute switch: its own, or (for a voice) the hub's. Any thread.
    bool mutedByOwnSwitch() const;
    bool mutedByHub() const;
    /// Voice: asks the hub to bring its window to the front, as far as the host allows (SPEC 8.1).
    void showHub();
    /// Message thread: brings the window of this instance's editor to the front, if there is one.
    /// Brings the editor window of this instance to the front; false when no editor is open (a host window that is
    /// closed cannot be opened from a plug-in).
    bool bringEditorToFront();
    /// A voice asked for the hub window and it is not open: the status line says what to do. Message thread.
    void noteHubWindowClosed() { generationStatus_ = GenerationStatus::HubWindowClosed; }
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
    /// Live transposition (SPEC 3.16, v1.1). The effective transposition is the host parameter `transpose` plus the
    /// offset the keyboard set, limited to +-12 (the keyboard offset is never written back into the parameter). The
    /// keyboard acts on hub and solo instances only and is off during an offline render; it is not passed on.
    /// `setTransposeSettings` is for the message thread, the audio thread reads it at each block.
    void setTransposeSettings(const mm::core::TransposeSettings& settings);
    mm::core::TransposeSettings transposeSettings() const;
    /// Takes the transposition settings from the settings file of the user (SPEC 8.4) when they changed since the last
    /// time; the timer calls it, tests call it to not wait. A store without file changes nothing.
    void syncTransposeSettings();
    /// The transposition this instance applies to its notes now (parameter + keyboard, 0 when the target is another
    /// voice). Any thread.
    int effectiveTranspose() const { return effectiveTranspose_.load(std::memory_order_relaxed); }
    /// The transposition in force for the whole group before the target: parameter plus keyboard (hub, solo) or what the
    /// voice follows. The export uses it. Any thread.
    int transposeTotal() const { return transposeTotal_.load(std::memory_order_relaxed); }
    /// The offset the keyboard set (without the parameter). Any thread.
    int keyboardTranspose() const { return keyboardTranspose_.load(std::memory_order_relaxed); }

    /// The Evolve layer (SPEC 6.7, v1.1), message thread, hub and solo only (a voice says "use the hub"). While it is on,
    /// a stage (a variation of the slot or of the stage before) is made every `EvolveSettings::everyPasses` passes
    /// of the pattern and plays from the pass after; the slot itself stays as it is. Without a playing transport
    /// nothing happens, and an offline render switches it off (SPEC 6.6). It is not saved with the project.
    struct EvolveInfo {
        bool on = false;
        bool layerActive = false; ///< a layer exists (on, with a pattern in the selected slot)
        uint32_t stage = 0;       ///< number of the stage that plays (0: the unchanged slot)
        bool canBack = false;
        bool changed = false; ///< a stage other than the slot sounds: "keep" does something
        bool operator==(const EvolveInfo&) const = default;
    };
    void setEvolve(bool on);
    bool evolveOn() const { return evolveOn_; }
    EvolveInfo evolveInfo() const;
    /// Takes the stage that sounds into the slot (a history entry and an undo step, SPEC 6.7); Evolve goes on from there.
    /// False when no stage other than the slot sounds.
    bool evolveKeep();
    /// Goes back one stage of the last 8; it plays at the next bar line. False at the end of the memory.
    bool evolveBack();
    /// Message thread: fixes the seed the layer takes for its stages (default: a random one per layer), so that the
    /// stages repeat. For tests and diagnosis.
    void setEvolveSeed(std::optional<uint64_t> seed) { evolveSeed_ = seed; }
    /// Message thread: the settings of the layer (clamped); they act from the next stage.
    void setEvolveSettings(const mm::core::EvolveSettings& settings);
    /// What the timer of the layer does every 40 ms: follows the slot, counts the passes of the pattern by the playhead
    /// and makes the next stage. Public so that tests drive it without waiting for the timer.
    void evolveTick();
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
    /// Message thread: the slot actions of SPEC 3.10 (0-based indices). Hub and solo only (a voice says "use the hub").
    /// `copySlot` puts the slot (pattern, name and colour) on the clipboard of this instance; `pasteSlot` makes it the
    /// result of a slot (history entry and an undo step, plays when that slot is selected); `copySlotTo` is both in
    /// one; `clearSlot` resets a slot completely and `swapSlots` exchanges two with their history. Clearing and
    /// swapping end the undo steps of the slots they touch (the steps no longer fit, D-153). False when nothing
    /// happened; `generationStatus()` says why.
    bool copySlot(size_t index);
    bool pasteSlot(size_t index);
    bool copySlotTo(size_t from, size_t to);
    bool clearSlot(size_t index);
    bool swapSlots(size_t first, size_t second);
    bool hasSlotClipboard() const;
    /// Message thread: the groove of a voice of the slot (0-based indices, SPEC 3.9): a template (`straight`,
    /// `drum_reference` or an id of the library) and/or the swing (0.50 to 0.75). Like a note edit: it plays at once and
    /// is an undo step (`mergeWithPrevious` continues the step of a slider that is dragged). False in a voice, for an
    /// empty slot, an unknown template or when nothing changes.
    bool setVoiceGroove(size_t slotIndex, size_t voice, const std::optional<std::string>& templateId,
                        std::optional<float> swing, bool mergeWithPrevious = false);
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
    /// The size of the editor of this instance in percent (SPEC 8.1, one of `kUiScaleSteps`), kept in the state. Message
    /// thread. It is not part of `InstanceSettings`: it concerns the window only, not the group.
    int uiScalePct() const { return uiScalePct_.load(); }
    void setUiScalePct(int percent) { uiScalePct_.store(mm::core::clampUiScale(percent)); }
    /// Whether the rolls follow the playhead (the window turns to the page that holds it). A setting of the instance,
    /// kept in the state; off by default.
    bool followPlayhead() const { return followPlayhead_.load(); }
    void setFollowPlayhead(bool follow) { followPlayhead_.store(follow); }
    void setInstanceSettings(const mm::core::InstanceSettings& settings);
    /// Message thread: generates a pattern in the background for the slot that is selected now (SPEC 3.1, D-140).
    /// The result lands in that slot and plays at the next bar line; during an offline bounce it waits until the
    /// host is real-time again. A new request replaces a running one.
    /// With an AI provider set in `AiBackend` the request goes to the AI (SPEC 7), else it runs offline. When the AI
    /// fails the status becomes `AiFailed` and the slot stays as it was: the editor offers `retryGeneration`,
    /// `generateOffline` or nothing (SPEC 7.9). With `autoOffline` the offline generator takes over by itself.
    void generate();
    /// Message thread: the same request without the AI (same settings and seed).
    void generateOffline();
    /// Message thread: sends the last request again, to the AI if it went there. No-op without an earlier request.
    void retryGeneration();
    /// Message thread: ends the running request; the slot stays as it was.
    void cancelGeneration();
    /// "Continue offline automatically" for the rest of this session (SPEC 7.9), per instance.
    void setAutoOffline(bool on) { autoOffline_ = on; }
    bool autoOffline() const { return autoOffline_; }
    /// True while the running request goes to an AI provider.
    bool generatingWithAi() const { return generationStatus_ == GenerationStatus::Generating && lastJobUsesAi_; }
    /// How the last request ended (message thread).
    const GenerationReport& lastReport() const { return lastReport_; }
    GenerationStatus generationStatus() const { return generationStatus_; }
    /// Message thread: varies the pattern of the slot that is selected now (SPEC 3.6, D-155) with the subtle operators,
    /// synchronously (it is purely algorithmic). `voice` limits it to one voice (0-based), `seed` makes it repeatable
    /// (default: random). The result is a history entry and an undo step and plays at the next bar line. Locked
    /// voices stay as they are. False, with the status telling why, for a voice instance, an empty slot, all voices
    /// locked, strength 0 or when nothing could change.
    /// `slot` (0-based) names another slot than the selected one: the editor passes the slot its rows show.
    bool vary(int strengthPct, std::optional<size_t> voice = std::nullopt, std::optional<uint64_t> seed = std::nullopt,
              std::optional<size_t> slot = std::nullopt);
    /// Message thread: generates the voice anew (0-based) with `regenerateVoice` (D-160): purely algorithmic, so
    /// synchronous, with a random seed unless `seed` is given. Harmony, kick grid and the other voices stay; the
    /// energy and creativity are those of the hub settings. The result is a history entry and an undo step and plays
    /// at the next bar line. False, with the status telling why, for a voice instance, an empty slot, a voice with all
    /// three locks or when the voice cannot be generated.
    bool renewVoice(size_t voice, std::optional<uint64_t> seed = std::nullopt,
                    std::optional<size_t> slot = std::nullopt);
    /// Message thread: browses the results of the slot (SPEC 3.6): the entry becomes the current pattern and plays at
    /// the next bar line. Browsing is no undo step; it removes the undo and redo steps of the slot, which would no
    /// longer fit (D-160). False at the ends, for an empty history and in a voice instance.
    bool historyBack(std::optional<size_t> slot = std::nullopt);
    bool historyForward(std::optional<size_t> slot = std::nullopt);
    /// Message thread: refines the pattern of the slot with the AI (SPEC 3.15, D-173): the pattern, the style, the
    /// locks and the last 5 refinements of the slot go with the `instruction`. `voice` or `phrase` (0-based) limit the
    /// scope. Runs in the background like "Generate" (cancel, retry); the result is a history entry and an undo step
    /// and plays at the next bar line. There is no offline refinement. False, with the status telling why, for a voice
    /// instance, an empty slot or instruction, a scope that is all locked and when no AI provider is set up.
    /// `slot` (0-based) names another slot than the selected one.
    bool refine(const std::string& instruction, std::optional<size_t> voice = std::nullopt,
                std::optional<size_t> phrase = std::nullopt, std::optional<size_t> slot = std::nullopt);
    /// What a refinement of the slot can aim at: its voices (roles, in pattern order) and its phrases (start bar and
    /// length in bars). Empty for an empty slot. Message thread.
    struct RefineTargets {
        std::vector<mm::core::VoiceRole> voices;
        std::vector<std::pair<uint32_t, uint32_t>> phrases;
        bool operator==(const RefineTargets&) const = default;
    };
    RefineTargets refineTargets(std::optional<size_t> slot = std::nullopt) const;
    /// A MIDI clip that was read and checked for a voice (SPEC 3.18): the plan with the notes as they are (ticks exact,
    /// length rounded up to a pattern length, at most 16 bars) and where it is meant to go.
    struct PendingImport {
        size_t voice = 0; ///< 0-based voice of the pattern
        int slot = 0;     ///< 0-based slot
        std::string fileName;
        mm::core::ImportPlan plan;
    };
    /// Message thread: reads a dropped MIDI file for `voice` (0-based) in the background and checks it (4/4, length,
    /// notes). The outcome is the generation status (`ImportRead` and the like) and, on success, `pendingImport()`.
    /// False for a file that cannot be a MIDI file by its name. A voice instance imports for its own voice.
    bool importMidi(const juce::File& file, std::optional<size_t> voice = std::nullopt);
    const std::optional<PendingImport>& pendingImport() const { return pendingImport_; }
    /// Message thread: like `importMidi`, and once the file is read the clip replaces the voice in the slot that was
    /// selected at the drop (SPEC 3.18, D-178): locked in every dimension, the key, scale and progression of the slot
    /// and the settings come from the analysis, the other voices are generated around it. One undo step for the import,
    /// one for the generation. A voice instance gets `UseHub`.
    bool importVoice(const juce::File& file, std::optional<size_t> voice = std::nullopt);
    /// The drum reference that new and generated patterns take (SPEC 3.14: new slots take the one used last).
    const std::optional<mm::core::RhythmReference>& drumReference() const { return drumReference_; }
    /// Message thread: takes the drum reference out of the slot (selected one, or `slot`, 0-based) and forgets it, so
    /// that the next generation does not bring it back. False for a voice instance.
    bool removeDrumReference(std::optional<size_t> slot = std::nullopt);
    /// Message thread: the musician corrects the key of a slot with imported (locked) voices: the root and the scale
    /// are set, the progression is derived again from the locked voices. False for an unknown scale, a slot without
    /// locked voice, a voice instance.
    bool correctKey(mm::core::PitchClass root, const std::string& scaleId, std::optional<size_t> slot = std::nullopt);
    /// The library (SPEC 3.12, D-184), message thread, hub and solo only (a voice instance says `UseHub`). The outcome
    /// is the generation status (`Library...`). Saving works on the slot (default: the active one) and a set on all.
    /// A flag that turns false when this instance is destroyed: a library window that outlives the editor checks it.
    std::shared_ptr<bool> libraryAliveFlag() const { return libraryAlive_; }
    bool saveToLibrary(const std::string& name, std::optional<size_t> slot = std::nullopt);
    bool saveSetToLibrary(const std::string& name);
    /// Reads the entry in the background and puts it into a slot (a pattern; default: the active slot) or into the
    /// slots a set holds (the others stay). Both are results: the pattern that was there stays in the history.
    bool loadFromLibrary(const std::string& id, std::optional<size_t> slot = std::nullopt);
    /// Writes the pattern entry as a .mid file at the DAW tempo, in the background.
    bool exportLibraryEntry(const std::string& id, const juce::File& target);
    /// Reads a MIDI file in the background and saves it as a library entry (a pattern with the file as a locked voice).
    bool importMidiToLibrary(const juce::File& file);
    /// Message thread: the pending import is used up (or dismissed).
    void clearPendingImport() { pendingImport_.reset(); }
    /// True when the last request was a refinement (message thread).
    bool lastJobIsRefine() const { return lastJob_ && lastJob_->refine; }
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
    /// Message thread: a trigger parameter asked for a variation of voice `voice` (1 to `kMaxVoices`, 0 = all) with the
    /// strength of the settings. A voice hands it to the hub. Public for the group link.
    void varyFromTrigger(int voice);
    /// Called by the group link on the message thread: a voice takes over the hub's Evolve stage (null: none) and plays
    /// it in that slot from the stamp (or the next bar line) on. Other roles ignore it. The stage is not kept.
    void adoptHubOverlay(const mm::core::OverlayPtr& overlay, std::optional<double> stampPpq = std::nullopt);
    /// Called by the group link on the message thread: a voice takes over the hub's slot set. Other roles ignore it.
    void adoptHubSlots(const mm::core::SlotSnapshot& snapshot, std::optional<double> stampPpq = std::nullopt);
    /// Slots that were left empty by the last state load because their data was bad (path and reason).
    std::vector<std::string> slotLoadProblems() const;
    /// Version of the pattern that is playing (0 for the built-in placeholder). Safe to read from any thread.
    uint64_t activePatternVersion() const;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /// What a hub tells its voices about its Evolve stage (sent after the instance lock is released).
    struct OverlayMessage {
        mm::core::OverlayPtr overlay;
        std::optional<double> stamp;
    };
    bool stepUndo(bool back);
    bool browseHistory(bool back, std::optional<size_t> slot);
    void writeEvents(juce::MidiBuffer& midi) const;
    /// Audio thread: reads the keyboard events of the block (live transposition) and hands the transposition of this
    /// block to the player.
    void applyTransposition(const juce::MidiBuffer& input, const mm::engine::TransportInfo& transport, int numSamples);
    // Under `slotsLock_`: the playing slot (also fills slot, origin and revision of `view`), and one voice of a
    // pattern.
    const mm::core::Slot* playingSlotLocked(VoiceView& view) const;
    void fillVoiceLocked(VoiceView& view, const mm::core::Pattern& pattern, size_t voiceIndex) const;
    void startJob(GenerationJob job);
    void onGenerated(const GenerationJob& job, std::optional<mm::core::Pattern> pattern);
    bool isHubOrSolo() const;
    void applyLibraryEntry(size_t index, mm::core::LibraryLoadResult loaded);
    void saveImportedToLibrary(const ImportRequest& request, const mm::core::ImportPlan& plan);
    void onImported(const ImportRequest& request, ImportOutcome outcome);
    bool startImport(const juce::File& file, std::optional<size_t> voice, bool apply);
    void applyImport(const ImportRequest& request, const mm::core::ImportPlan& plan);
    void applyDrumReference(const ImportRequest& request, const mm::core::ImportPlan& plan);
    void applyGenerated(const GenerationJob& job, mm::core::Pattern pattern);
    void setInstanceSettingsLocked(const mm::core::InstanceSettings& settings);
    void updateSlotMode(); // message thread
    std::optional<OverlayMessage> evolveEnd(bool restoreSlot);
    void evolveCommitBefore(size_t slotIndex);
    uint64_t newEvolveSeed() const;
    bool evolveCompany() const; // under `slotsLock_`: a hub with voices around it
    std::optional<double> evolveBarStamp() const;
    /// Under `slotsLock_`: plays `stage` in `slot` (the hub's own engine) from the stamp or the next grid point and
    /// returns what goes to the voices (sent after the lock is released); null `stage`: the slot content again.
    OverlayMessage evolveShow(size_t slot, const mm::core::Pattern* stage, double gridPpq,
                              std::optional<double> stamp);
    void sendOverlay(const std::optional<OverlayMessage>& message);
    void evolveTickLocked(std::optional<OverlayMessage>& message);
    void onEvolveParameter(const juce::String& id, bool on);
    void resetTrigger(const juce::String& id);
    void setEvolveParameter(bool on);
    /// A new instance takes the key the user chose last (global settings, D-162) once the file is read.
    void applyStartKey();

    /// Empties the handover's return queue on the message thread (SPEC 6.3: patterns are freed there only).
    class ReturnCollector : private juce::Timer {
    public:
        explicit ReturnCollector(mm::engine::PatternHandover& handover);
        ~ReturnCollector() override;

    private:
        void timerCallback() override;
        mm::engine::PatternHandover& handover_;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::String name_;
    juce::AudioProcessorValueTreeState parameters_;
    std::atomic<float>* transposeValue_ = nullptr;                       // the `transpose` parameter, raw
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
    std::optional<GenerationJob> lastJob_;                       // message thread: for retry and offline fallback
    bool lastJobUsesAi_ = false;
    bool autoOffline_ = false;
    GenerationReport lastReport_;
    struct ParkedResult {
        GenerationJob job;
        mm::core::Pattern pattern;
    };
    std::optional<ParkedResult> parked_; // a result that arrived during an offline render (message thread)
    class ParkTimer;
    std::unique_ptr<ParkTimer> parkTimer_;
    std::atomic<bool> followPlayhead_{false};
    std::atomic<int> uiScalePct_{mm::core::kDefaultUiScale}; // the host may set the state from any thread
    bool startKeyPending_ = true;              // message thread
    std::unique_ptr<ParkTimer> startKeyTimer_; // message thread
    // The Evolve layer (message thread; `evolveLayer_` and the counters under `slotsLock_`)
    bool evolveOn_ = false;
    std::optional<uint64_t> evolveSeed_;
    mm::core::EvolveLayer evolveLayer_;
    uint64_t evolveRevision_ = 0;           // revision of the slot the layer started from
    bool evolveStagePublished_ = false;     // the engine holds a stage that is not the slot content
    std::optional<int64_t> evolveLastPass_; // the pass of the pattern the last tick saw
    int evolvePasses_ = 0;                  // passes seen since the layer started
    bool evolveAdvanceDue_ = false;         // the next stage is due: it is made in the middle of the pass
    std::unique_ptr<ParkTimer> evolveTimer_;
    class EvolveParameterListener;
    std::unique_ptr<EvolveParameterListener> evolveListener_;
    std::atomic<bool> loadingState_{false}; // the host sets the parameters from a saved state: no Evolve events
    mm::core::OverlayPtr voiceOverlay_;     // voice: the stage the hub plays (message thread, under `slotsLock_`)
    mm::engine::PatternPlayer player_;
    ReturnCollector returnCollector_;
    mm::engine::MidiEventList events_;
    std::atomic<double> lastBpm_{120.0};
    mm::engine::PlayheadMeter playhead_;
    MidiExporter exporter_;
    struct ExportKey {
        uint64_t origin = 0;
        uint64_t revision = 0;
        int slot = 0;
        int voice = 0;
        int octave = 0;
        long long bpmCentis = 0;
        bool all = false; ///< every voice is exported (Solo and Hub), not only the own one
        int transpose = 0;       ///< the transposition the notes carry (0: none or not exported)
        int mode = 0;
        bool empty = true;
        bool operator==(const ExportKey&) const = default;
    };
    std::optional<ExportKey> exportKey_; // message thread
    std::atomic<uint32_t> frontRequests_{0};

    mm::engine::GroupSync groupSync_{mm::engine::processGroupChannel(), mm::engine::GroupChannel::kNone};
    // Live transposition: the settings packed in one word (message thread writes, audio thread reads), the state of
    // the keyboard (audio thread only) and what it came to (read by the UI).
    std::atomic<uint32_t> transposeConfig_{0};
    uint64_t transposeRevision_ = 0;                // message thread: the settings file version taken last
    std::unique_ptr<ParkTimer> transposeSyncTimer_; // message thread
    struct KeyboardTranspose {
        int current = 0;      // keyboard offset in effect
        bool pending = false; // a change that waits for its point (beat, bar, or the group's planned stamp)
        int pendingValue = 0;
        int pendingParam = 0; // the host parameter at the time it was planned (a group applies both at the stamp)
        double pendingAtPpq = 0.0;
        int appliedParam = 0; // the host parameter in force (in a group it follows the plan, not the host at once)
        int paramSeen = 0;
        int heldKey = -1; // the key that set the offset (momentary: its release resets it)
        // A voice that follows the hub: the hub's total in force, a change that waits for its stamp, what it has heard
        int groupApplied = 0;
        bool groupPending = false;
        int groupPendingValue = 0;
        double groupPendingAtPpq = 0.0;
        uint32_t seenPlan = 0;
        int knownHub = -2;
        bool needSync = true;
    };
    KeyboardTranspose keyboard_;
    std::atomic<int> effectiveTranspose_{0};
    std::atomic<int> keyboardTranspose_{0};
    std::atomic<int> transposeTotal_{0};
    std::atomic<int> slotMode_{static_cast<int>(mm::engine::SlotMode::Own)}; // read in the audio thread
    std::atomic<bool> silent_{false};    // a hub with output mode "none" plays nothing (audio thread reads)
    std::atomic<bool> voiceRole_{false}; // a voice also obeys the hub's mute switches (audio thread reads)
    int channelMember_ = mm::engine::GroupChannel::kNone; // message thread
    std::shared_ptr<GroupLink> groupLink_;
    std::optional<PendingImport> pendingImport_;
    std::optional<mm::core::RhythmReference> drumReference_; ///< the last one dropped (message thread)
    std::optional<mm::core::SlotContent> slotClipboard_; // message thread
    std::shared_ptr<bool> libraryAlive_ = std::make_shared<bool>(true); ///< callbacks of the library check it
    bool importCut_ = false; ///< the import that started the running generation was cut to 16 bars
    MidiImportService importer_;  // before the generator: both are destroyed before the members they call into
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
