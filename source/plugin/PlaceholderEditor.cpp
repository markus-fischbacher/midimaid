#include "plugin/PlaceholderEditor.h"

namespace mm::plugin {

/// Grip area: dragging it starts an external drag of the exported file (no text, icon only).
class PlaceholderEditor::DragHandle : public juce::Component {
public:
    explicit DragHandle(MidiExporter& exporter) : exporter_(exporter) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff2c2c33));
        g.fillRoundedRectangle(bounds, 6.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        for (int row = -1; row <= 1; ++row) {
            for (int col = -2; col <= 2; ++col) {
                g.fillEllipse(cx + static_cast<float>(col) * 8.0f - 1.5f, cy + static_cast<float>(row) * 8.0f - 1.5f,
                              3.0f, 3.0f);
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (dragging_ || event.getDistanceFromDragStart() < 4) {
            return;
        }
        const auto file = exporter_.readyFile();
        if (file == juce::File()) {
            return; // first export still running
        }
        dragging_ = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, false, this,
                                                                   [this] { dragging_ = false; });
    }

private:
    MidiExporter& exporter_;
    bool dragging_ = false;
};

PlaceholderEditor::PlaceholderEditor(ProcessorBase& processor)
    : juce::AudioProcessorEditor(processor), processor_(processor),
      dragHandle_(std::make_unique<DragHandle>(processor.midiExporter())) {
    addAndMakeVisible(*dragHandle_);
#if MIDIMAID_KEY_PROBE
    setWantsKeyboardFocus(true);
    probeStartMs_ = juce::Time::getMillisecondCounter();
    probeLog("probe started, click into the window to give it the keyboard focus");
#endif
    setSize(600, 320);
    processor_.midiExporter().requestExport(processor_.lastKnownBpm());
    startTimerHz(2); // keep the exported tempo equal to the DAW tempo
}

PlaceholderEditor::~PlaceholderEditor() {
    stopTimer();
}

void PlaceholderEditor::timerCallback() {
    const double bpm = processor_.lastKnownBpm();
    auto& exporter = processor_.midiExporter();
    if (std::abs(exporter.exportedBpm() - bpm) > 0.01) {
        exporter.requestExport(bpm);
    }
}

void PlaceholderEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1b1b1f));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawText(getAudioProcessor()->getName(), getLocalBounds().removeFromTop(48), juce::Justification::centred);
#if MIDIMAID_KEY_PROBE
    g.setColour(juce::Colour(0xffffb347));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(juce::String("KEY PROBE (focus: ") + (hasKeyboardFocus(false) ? "yes" : "no") + ")",
               getLocalBounds().removeFromTop(64).withTrimmedTop(44), juce::Justification::centred);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(68);
    area.removeFromBottom(56);
    for (const auto& line : probeLines_) {
        g.drawText(line, area.removeFromTop(16), juce::Justification::centredLeft);
    }
#endif
}

void PlaceholderEditor::resized() {
    dragHandle_->setBounds(getLocalBounds().removeFromBottom(56).reduced(16));
}

#if MIDIMAID_KEY_PROBE
void PlaceholderEditor::probeLog(const juce::String& text) {
    const auto seconds = static_cast<double>(juce::Time::getMillisecondCounter() - probeStartMs_) / 1000.0;
    probeLines_.push_back(juce::String(seconds, 2).paddedLeft(' ', 7) + " s  " + text);
    if (probeLines_.size() > static_cast<size_t>(kProbeLines)) {
        probeLines_.erase(probeLines_.begin());
    }
    repaint();
}

void PlaceholderEditor::mouseDown(const juce::MouseEvent&) {
    grabKeyboardFocus();
}

bool PlaceholderEditor::keyPressed(const juce::KeyPress& key) {
    probeLog("press   " + key.getTextDescription() + "  (code " + juce::String(key.getKeyCode()) + ")");
    return false; // never consume: the host decides what it does with the key
}

bool PlaceholderEditor::keyStateChanged(bool isKeyDown) {
    probeLog(isKeyDown ? "state   a key went down" : "state   a key went up");
    return false;
}

void PlaceholderEditor::modifierKeysChanged(const juce::ModifierKeys& modifiers) {
    probeLog("modifiers  shift=" + juce::String(modifiers.isShiftDown() ? 1 : 0) + " ctrl=" +
             juce::String(modifiers.isCtrlDown() ? 1 : 0) + " alt=" + juce::String(modifiers.isAltDown() ? 1 : 0) +
             " cmd=" + juce::String(modifiers.isCommandDown() ? 1 : 0));
}

void PlaceholderEditor::focusGained(FocusChangeType) {
    probeLog("focus gained");
}

void PlaceholderEditor::focusLost(FocusChangeType) {
    probeLog("focus lost");
}
#endif

} // namespace mm::plugin
