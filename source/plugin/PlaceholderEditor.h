#pragma once

#include "plugin/ProcessorBase.h"
#include "plugin/RollView.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Minimal editor until the hub UI exists: the plugin name, role and voice pick lists, a style pick list with a
/// Generate button and a status line (D-140, D-141), and a drag handle that drags the exported .mid file into the host.
/// A voice gets the compact voice UI of SPEC 8.1 instead (D-148): a read-only roll of the voice it plays, Mute, octave,
/// the slot follow switch, "Open hub" and the drag handle.
/// The texts are fixed English for now; they move to the translation table with the i18n infrastructure (phase 2).
class PlaceholderEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PlaceholderEditor(ProcessorBase& processor);
    ~PlaceholderEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class DragHandle;

    void timerCallback() override;

    void updateStatus();
    /// Switches between the voice UI and the placeholder layout (size and visible controls).
    void applyLayout(bool voiceLayout);
    void updateVoiceView();

    ProcessorBase& processor_;
    juce::ComboBox roleBox_;
    juce::ComboBox voiceBox_;
    juce::ComboBox followBox_;
    juce::ComboBox outputBox_; // hub only: plays a voice or nothing
    juce::ComboBox styleBox_;
    juce::TextButton takeOverButton_{"Become hub"};
    juce::TextButton generateButton_{"Generate"};
    juce::TextButton openHubButton_{"Open hub"};
    juce::ToggleButton muteButton_{"Mute"};
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
