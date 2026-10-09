#pragma once

#include "plugin/EditorParts.h"
#include "plugin/ProcessorBase.h"
#include "plugin/RollView.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// The editor until the full hub UI of later phases exists (SPEC 8.1). Solo and Hub get the simple hub UI (D-151):
/// role, output, style, key, scale, bars, seed, energy, creativity and Generate; the 16 slots; per voice of the playing
/// slot a read-only roll with Mute and a grip that drags that voice's file into the host; an info line and the status
/// line. A voice gets the compact voice UI instead (D-148): a read-only roll of the voice it plays, Mute, octave, the
/// slot follow switch, "Open hub" and the drag handle. The texts come from the translation table (SPEC 8.2).
class PlaceholderEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PlaceholderEditor(ProcessorBase& processor);
    ~PlaceholderEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    /// The voice (1 to 8) that has the focus in the roll area of the hub UI, 0 when all are shown.
    int focusedVoice() const { return focusedVoice_; }
    /// The strength (0 to 100 %) that "Variation" uses; a value of the open editor, not saved (D-161).
    /// True while the expert level is shown (SPEC 8.1); the basic level is the default.
    bool expertLevel() const { return expert_; }
    int variationStrength() const { return static_cast<int>(strengthSlider_.getValue()); }

private:
    void timerCallback() override;

    void updateStatus();
    /// Switches between the voice UI and the placeholder layout (size and visible controls).
    void applyLayout(bool voiceLayout);
    void updateVoiceView();
    /// The hub UI: fields, slot strip, rows of the playing slot, info line.
    void updateFullView();
    void syncFields(const mm::core::InstanceSettings& settings);
    void rebuildRows(size_t count);
    /// Hands the grid and the pitch snapping to every roll of the hub UI.
    void applyEditSettings();
    /// Track focus (D-159): the voice that has the whole height, 0 for all.
    void setFocusedVoice(int voice);
    void applyFocus();
    /// Buttons for the actions (D-161): variation of one voice or of all with the strength of the slider, and the
    /// state of Undo, Redo and the history browser.
    void varyVoice(std::optional<size_t> voice, std::optional<size_t> slot);
    void updateActionButtons();
    /// The levels of the hub UI (SPEC 8.1, D-162): shows or hides the expert controls.
    void applyLevel();
    /// Takes the values of the user's settings file once it is read (level, strength, grid, snapping).
    void applyStoredSettings();
    void saveEditSettings();

    ProcessorBase& processor_;
    juce::ComboBox roleBox_;
    juce::ComboBox voiceBox_;
    juce::ComboBox followBox_;
    juce::ComboBox outputBox_; // hub only: plays a voice or nothing
    juce::ComboBox styleBox_;
    juce::ComboBox keyBox_;
    juce::ComboBox scaleBox_;
    juce::ComboBox barsBox_;
    juce::ComboBox gridBox_; // hub: the edit grid of the piano rolls
    juce::ToggleButton tripletButton_;
    juce::ComboBox snapBox_;
    juce::ToggleButton expertButton_;
    juce::TextButton varyAllButton_;
    juce::Label strengthLabel_;
    juce::Slider strengthSlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::TextButton undoButton_;
    juce::TextButton redoButton_;
    juce::TextButton historyBackButton_;
    juce::TextButton historyForwardButton_;
    juce::Label historyLabel_;
    juce::TextEditor seedEditor_;
    juce::TextButton randomButton_;
    juce::Label energyLabel_;
    juce::Label creativityLabel_;
    juce::Slider energySlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::Slider creativitySlider_{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    std::array<juce::TextButton, mm::core::kSlotCount> slotButtons_;
    std::vector<std::unique_ptr<VoiceRow>> rows_;
    std::vector<ProcessorBase::VoiceView> shownVoices_;
    ProcessorBase::SlotInfo shownInfo_;
    juce::Label infoLabel_;
    juce::TextButton takeOverButton_;
    juce::TextButton generateButton_;
    juce::TextButton openHubButton_;
    juce::ToggleButton muteButton_;
    juce::ComboBox octaveBox_;
    RollView rollView_;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment_;
    int muteVoice_ = 0; // the voice the mute button is bound to
    int focusedVoice_ = 0;
    bool expert_ = false;
    bool settingsApplied_ = false;
    bool voiceLayout_ = false;
    ProcessorBase::VoiceView shown_; // what the roll shows now
    juce::Label statusLabel_;
    juce::String groupText_;
    std::unique_ptr<DragHandle> dragHandle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
