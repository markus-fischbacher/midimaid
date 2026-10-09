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

    ProcessorBase& processor_;
    juce::ComboBox roleBox_;
    juce::ComboBox voiceBox_;
    juce::ComboBox followBox_;
    juce::ComboBox outputBox_; // hub only: plays a voice or nothing
    juce::ComboBox styleBox_;
    juce::ComboBox keyBox_;
    juce::ComboBox scaleBox_;
    juce::ComboBox barsBox_;
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
    bool voiceLayout_ = false;
    ProcessorBase::VoiceView shown_; // what the roll shows now
    juce::Label statusLabel_;
    juce::String groupText_;
    std::unique_ptr<DragHandle> dragHandle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
