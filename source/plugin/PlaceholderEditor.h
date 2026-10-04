#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Minimal editor for Phase 0: shows only the plugin name. The real UI follows in later phases.
class PlaceholderEditor : public juce::AudioProcessorEditor {
public:
    explicit PlaceholderEditor(juce::AudioProcessor& processor);

    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
