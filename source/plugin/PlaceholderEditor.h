#pragma once

#include "plugin/ProcessorBase.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Minimal editor until the hub UI exists: the plugin name, a style pick list with a Generate button and a status
/// line (D-140), and a drag handle that drags the exported .mid file into the host. The texts are fixed English for
/// now; they move to the translation table with the i18n infrastructure (phase 2).
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

    ProcessorBase& processor_;
    juce::ComboBox styleBox_;
    juce::TextButton generateButton_{"Generate"};
    juce::Label statusLabel_;
    std::unique_ptr<DragHandle> dragHandle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
