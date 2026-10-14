#pragma once

#include "plugin/EditorParts.h"
#include "plugin/MidiMaidLookAndFeel.h"
#include "plugin/ProcessorBase.h"
#include "plugin/RollView.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace mm::plugin {

/// The editor until the full hub UI of later phases exists (SPEC 8.1). Solo and Hub get the simple hub UI (D-151):
/// role, output, style, key, scale, bars, seed, energy, creativity and Generate; the 16 slots; per voice of the playing
/// slot a read-only roll with Mute and a grip that drags that voice's file into the host; an info line and the status
/// line. A voice gets the compact voice UI instead (D-148): a read-only roll of the voice it plays, Mute, octave, the
/// slot follow switch, "Open hub" and the drag handle. The texts come from the translation table (SPEC 8.2).
class PlaceholderEditor : public juce::AudioProcessorEditor,
                          private juce::Timer,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropContainer {
public:
    explicit PlaceholderEditor(ProcessorBase& processor);
    ~PlaceholderEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    // A MIDI file dropped on the voice window imports for its voice (the hub takes drops on its rows, SPEC 3.18).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    /// The voice (1 to 8) that has the focus in the roll area of the hub UI, 0 when all are shown.
    int focusedVoice() const { return focusedVoice_; }
    /// The strength (0 to 100 %) that "Variation" uses; a value of the open editor, not saved (D-161).
    /// True while the expert level is shown (SPEC 8.1); the basic level is the default.
    bool expertLevel() const { return expert_; }
    /// A key press (SPEC 8.3, D-235): the action the user gave the key in the settings runs; false when the key has no
    /// action, when it has no modifier and a text field has the focus, or in the voice window. Keys the user did not
    /// assign are never taken, so the host keeps them.
    bool keyPressed(const juce::KeyPress& press) override;
    /// Runs the action with this id (`core/Shortcuts`); false for an unknown id or when nothing was there to act on.
    bool runShortcut(const std::string& action);
    int variationStrength() const { return static_cast<int>(strengthSlider_.getValue()); }

    /// The pages of the inspector, the column of details that is closed by default (D-252).
    enum class InspectorPage { Instance, Pattern, Edit, Evolve, Group };
    bool inspectorOpen() const { return inspectorOpen_; }
    void setInspectorOpen(bool open);
    InspectorPage inspectorPage() const { return inspectorPage_; }
    void setInspectorPage(InspectorPage page);
    /// True while the dock refines the pattern of the slot instead of generating a new one.
    /// Opens the command palette over the hub window (D-254); the voice window has none.
    void openPalette();
    bool refining() const { return refineMode_ && shownInfo_.filled; }

private:
    void timerCallback() override;
    void refreshReferenceSets();
    std::string referenceStyleId() const;

    void updateStatus();
    /// Switches between the voice UI and the placeholder layout (size and visible controls).
    void applyLayout(bool voiceLayout);
    void applyScale();
    /// The size box shows the step that equals the size, or the size itself when it lies between two steps.
    void syncZoomBox();
    void layoutVoiceWindow();
    /// The playhead of the piano rolls (30 times a second): the host position as a line in the roll of the slot that
    /// plays, in the hub rows and in the voice window.
    void updatePlayhead();
    /// The menu of the slot actions at `where` (screen position) for the slot (1-based), SPEC 3.10.
    void showSlotMenu(int slot, juce::Point<int> where);
    void updateVoiceView();
    /// The hub UI: fields, slot strip, rows of the playing slot, info line.
    void updateFullView();
    /// Fills the group page from the registry (message thread).
    void updateGroupPage();
    /// The items of the voice box carry the names of the lanes: "1 · Bass", "2 · Melody", "Voice 3" (D-258).
    void updateVoiceNames();
    void updateRefineControls();
    /// The menu of the roles a voice can be added with (D-250), under the "+ Voice" button.
    void showAddVoiceMenu();
    /// The roles of the voices the rows show: the pattern's, or the settings' while the slot is empty.
    std::vector<mm::core::VoiceRole> shownRoles(const std::vector<ProcessorBase::VoiceView>& views) const;
    void startRefine();
    void syncFields(const mm::core::InstanceSettings& settings);
    void rebuildRows(size_t count);
    void correctSlotKey(const mm::core::InstanceSettings& settings);
    /// Hands the grid and the pitch snapping to every roll of the hub UI.
    void applyEditSettings();
    void applyRollHelpers();
    /// Track focus (D-159): the voice that has the whole height, 0 for all.
    void setFocusedVoice(int voice);
    void applyFocus();
    /// Buttons for the actions (D-161): variation of one voice or of all with the strength of the slider, and the
    /// state of Undo, Redo and the history browser.
    void varyVoice(std::optional<size_t> voice, std::optional<size_t> slot);
    void updateActionButtons();
    void updatePreviewButton();
    /// The levels of the hub UI (SPEC 8.1, D-162): shows or hides the expert controls.
    void applyLevel();
    /// Takes the values of the user's settings file once it is read (level, strength, grid, snapping).
    void applyStoredSettings();
    void saveEditSettings();

    /// Everything but the lanes, the header and the dock sits in the inspector on the right (D-252).
    static constexpr int kInspectorWidth = 300;
    static constexpr size_t kOpenRollsMax = 2; ///< more voices than this: one open roll, the others strips
    /// Shows the controls of the open inspector page, the dock of the current mode and hides the rest (hub layout).
    void applyPanels();

    ProcessorBase& processor_;
    MidiMaidLookAndFeel lookAndFeel_; // declared before the components that use it
    struct PlayheadTicker : juce::Timer {
        std::function<void()> tick;
        void timerCallback() override { tick(); }
    } playheadTicker_;
    juce::ComboBox roleBox_;
    juce::ComboBox voiceBox_;
    juce::ComboBox followBox_;
    juce::ComboBox outputBox_; // hub only: plays a voice or nothing
    juce::ComboBox styleBox_;
    juce::ComboBox referenceSetBox_; ///< the active reference set of the style (SPEC 3.20), hub and solo
    std::vector<std::string> referenceSetIds_;
    std::string referenceSetKey_;    ///< style and store revision the box shows
    juce::ComboBox keyBox_;
    juce::ComboBox scaleBox_;
    juce::ComboBox barsBox_;
    juce::ComboBox gridBox_; // hub: the edit grid of the piano rolls
    juce::ToggleButton tripletButton_;
    juce::ComboBox snapBox_;
    juce::ToggleButton expertButton_;
    juce::TextButton varyAllButton_;
    GroupPanel groupPanel_{processor_}; ///< the group page of the inspector (D-255)
    VoiceBar voiceBar_;              ///< the eight voices as dots above the rows (D-250)
    juce::TextButton addVoiceButton_;    ///< "+ Voice": the menu of the roles to add
    juce::TextButton removeVoiceButton_; ///< takes the last voice out (never bass and melody)
    juce::TextButton inspectorButton_;   ///< opens and closes the inspector
    juce::TextButton commandsButton_;    ///< opens the command palette (D-254)
    CommandPalette palette_;             ///< the search for every rare action
    std::array<juce::TextButton, 5> inspectorTabs_; ///< one per `InspectorPage`
    juce::TextButton modeButton_;        ///< the dock: write a new pattern or change the one of the slot
    bool inspectorOpen_ = false;
    InspectorPage inspectorPage_ = InspectorPage::Pattern;
    bool refineMode_ = false;
    juce::Label strengthLabel_;
    juce::Slider strengthSlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::TextButton previewButton_; // hub and solo: plays the slot with the internal clock while the host stands still
    juce::TextButton undoButton_;
    juce::TextButton redoButton_;
    juce::TextButton referencesButton_;
    juce::TextButton libraryButton_;
    juce::TextButton drumsButton_;
    juce::TextButton historyBackButton_;
    juce::TextButton historyForwardButton_;
    juce::Label historyLabel_;
    juce::TextEditor promptEditor_; // hub: what the AI is asked for (D-168)
    juce::TextEditor refineEditor_; // hub: what to change in the pattern of the slot (SPEC 3.15, D-173)
    juce::ComboBox refineScopeBox_; // hub: the whole pattern, one voice or one phrase
    juce::TextButton refineButton_;
    juce::TextButton settingsButton_;      // opens the settings dialog
    juce::TextButton errorSettingsButton_; // in the error bar: to the settings (invalid key, model)
    juce::TextButton retryButton_;
    juce::TextButton offlineButton_;
    juce::ToggleButton autoOfflineButton_;
    juce::TextEditor seedEditor_;
    juce::TextButton randomButton_;
    juce::ComboBox quantBox_; // expert level: where a switch may happen (SPEC 3.4, D-247)
    juce::ComboBox startBox_; // expert level: a new pattern restarts at the switch or continues the position (SPEC 3.4)
    juce::Label hatModeLabel_;
    juce::ComboBox hatModeBox_; // expert level: melody rhythm relative to the hats (SPEC 3.14)
    juce::Label chromaticLabel_, personalLabel_;
    juce::Slider personalSlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight}; // expert: how strongly the reference set acts (SPEC 3.20)
    juce::Slider chromaticSlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight}; // expert: -1 = the style's
    juce::Label energyLabel_;
    juce::Label creativityLabel_;
    juce::Slider energySlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::Slider creativitySlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    std::vector<std::unique_ptr<SlotButton>> slotButtons_;
    std::vector<std::unique_ptr<VoiceRow>> rows_;
    std::vector<ProcessorBase::VoiceView> shownVoices_;
    ProcessorBase::SlotInfo shownInfo_;
    int shownTranspose_ = 0; // the live transposition the info line shows
    ProcessorBase::RefineTargets shownTargets_;
    bool refinePending_ = false; // a refinement was started and its text stays until it is done
    juce::Label infoLabel_;
    juce::ComboBox ratingBox_;      // the stars of the pattern of the slot (SPEC 3.12, D-246)
    bool ratingUpdating_ = false;   // the box is being set from the processor: no click to send
    juce::TextButton takeOverButton_;
    juce::TextButton generateButton_;
    juce::TextButton openHubButton_;
    juce::ToggleButton muteButton_;
    juce::ComboBox octaveBox_;
    juce::ComboBox zoomBox_; // the size of the window (SPEC 8.1), both layouts
    ScaleCorner scaleCorner_; // continuous size by dragging the corner, both layouts
    juce::TextButton helpButton_; // opens the help dialog (SPEC 6.5)
    juce::TextButton followButton_; // the rolls follow the playhead page by page
    // Evolve (SPEC 6.7): the toggle, back, the stage and keep in the history bar; the settings in the expert row.
    juce::TextButton evolveButton_;
    juce::TextButton evolveBackButton_;
    juce::Label evolveStageLabel_;
    juce::TextButton evolveKeepButton_;
    juce::ComboBox evolveEveryBox_;
    juce::ToggleButton evolveOriginalButton_;
    juce::Label evolveStrengthLabel_;
    juce::Slider evolveStrengthSlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    void applyEvolveSettings();
    void syncTimeView(uint32_t startTick, uint32_t spanTicks);
    int statusButtons_ = 0; // which of the buttons beside the status message are shown
    int cornerStartPct_ = 100;
    double cornerStartWidth_ = 0.0;
    RollView rollView_;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment_;
    int muteVoice_ = 0; // the voice the mute button is bound to
    int focusedVoice_ = 0;
    bool expert_ = false;
    bool settingsApplied_ = false;
    uint64_t namingRevision_ = 0; ///< the settings revision the roll key labels were taken from
    bool voiceLayout_ = false;
    juce::Rectangle<int> voiceStrip_; ///< the strip of the voice window that paints as a surface
    ProcessorBase::VoiceView shown_; // what the roll shows now
    juce::Label statusLabel_;
    juce::String groupText_;
    std::unique_ptr<DragHandle> dragHandle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
