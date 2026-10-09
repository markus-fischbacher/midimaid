#pragma once

#include "plugin/EditableRoll.h"
#include "plugin/MidiExporter.h"
#include "plugin/ProcessorBase.h"
#include "plugin/RollView.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Grip area: dragging it starts an external drag of an exported file (no text, icon only). `voice` 0 drags the file of
/// the output voice, 1 to `kMaxVoices` the file of that voice.
class DragHandle : public juce::Component {
public:
    explicit DragHandle(MidiExporter& exporter, int voice = 0) : exporter_(exporter), voice_(voice) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& event) override;

    /// The file that a drag would start with now (empty while the first export is still running).
    juce::File file() const;

private:
    MidiExporter& exporter_;
    int voice_;
    bool dragging_ = false;
};

/// One voice of the hub UI (SPEC 8.1): name, Mute (host parameter `mute_<voice>`), the piano roll and the grip to
/// drag the voice's file into the host, and the lock of the voice (SPEC 3.5).
class VoiceRow : public juce::Component {
public:
    VoiceRow(ProcessorBase& processor, int voice);

    int voice() const { return voice_; }
    /// `view` is null for a slot without pattern.
    void setView(const ProcessorBase::VoiceView* view, const juce::String& fallbackName, bool fallbackMelody);
    /// Dim the roll while the voice is muted. Message thread.
    void updateMuted();
    EditableRoll& roll() { return roll_; }

    void resized() override;

private:
    ProcessorBase& processor_;
    int voice_;
    juce::Label nameLabel_;
    juce::ToggleButton muteButton_;
    juce::ToggleButton lockButton_;
    int slotIndex_ = -1; // the slot the row shows (0-based), -1 while there is none
    EditableRoll roll_;
    DragHandle drag_;
    std::unique_ptr<juce::ButtonParameterAttachment> muteAttachment_;
};

} // namespace mm::plugin
