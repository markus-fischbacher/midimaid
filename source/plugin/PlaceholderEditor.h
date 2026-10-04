#pragma once

#include "plugin/ProcessorBase.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Minimal editor for Phase 0: the plugin name and a drag handle that drags the exported .mid file into the host.
/// The real UI follows in later phases.
class PlaceholderEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PlaceholderEditor(ProcessorBase& processor);
    ~PlaceholderEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class DragHandle;

    void timerCallback() override;

    ProcessorBase& processor_;
    std::unique_ptr<DragHandle> dragHandle_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
