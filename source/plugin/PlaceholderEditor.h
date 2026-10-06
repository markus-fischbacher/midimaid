#pragma once

#include "plugin/ProcessorBase.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Minimal editor for Phase 0: the plugin name and a drag handle that drags the exported .mid file into the host.
/// The real UI follows in later phases.
///
/// With `MIDIMAID_KEY_PROBE` (CMake option, off by default) the editor also lists the keys the host forwards to the
/// plugin window (Phase 0 host verification, roadmap "Welche Tasten ...", B10 and C8). Diagnostic only: the texts are
/// not translated and the code goes away together with this editor.
class PlaceholderEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PlaceholderEditor(ProcessorBase& processor);
    ~PlaceholderEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

#if MIDIMAID_KEY_PROBE
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;
#endif

private:
    class DragHandle;

    void timerCallback() override;

    ProcessorBase& processor_;
    std::unique_ptr<DragHandle> dragHandle_;

#if MIDIMAID_KEY_PROBE
    void probeLog(const juce::String& text);

    static constexpr int kProbeLines = 12;
    std::vector<juce::String> probeLines_; // newest last
    juce::uint32 probeStartMs_ = 0;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderEditor)
};

} // namespace mm::plugin
