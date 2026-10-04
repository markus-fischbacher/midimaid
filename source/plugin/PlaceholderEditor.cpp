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
    g.drawText(getAudioProcessor()->getName(), getLocalBounds(), juce::Justification::centred);
}

void PlaceholderEditor::resized() {
    dragHandle_->setBounds(getLocalBounds().removeFromBottom(56).reduced(16));
}

} // namespace mm::plugin
