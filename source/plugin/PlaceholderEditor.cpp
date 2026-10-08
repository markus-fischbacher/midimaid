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

    const auto& profiles = processor_.styles().profiles();
    const auto current = processor_.instanceSettings().generation.styleId;
    for (size_t i = 0; i < profiles.size(); ++i) {
        styleBox_.addItem(juce::String(profiles[i].nameEn), static_cast<int>(i) + 1);
        if (profiles[i].id == current) {
            styleBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
    }
    if (styleBox_.getSelectedId() == 0 && !profiles.empty()) {
        styleBox_.setSelectedId(1, juce::dontSendNotification); // an unknown id generates with the first style
    }
    styleBox_.onChange = [this] {
        const auto& all = processor_.styles().profiles();
        const auto index = static_cast<size_t>(styleBox_.getSelectedId() - 1);
        if (index < all.size()) {
            auto settings = processor_.instanceSettings();
            settings.generation.styleId = all[index].id;
            processor_.setInstanceSettings(settings);
        }
    };
    addAndMakeVisible(styleBox_);

    generateButton_.onClick = [this] {
        processor_.generate();
        updateStatus();
    };
    addAndMakeVisible(generateButton_);

    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLabel_);

    setSize(600, 320);
    processor_.midiExporter().requestExport(processor_.lastKnownBpm());
    updateStatus();
    startTimerHz(4); // keep the exported tempo equal to the DAW tempo and the status line current
}

PlaceholderEditor::~PlaceholderEditor() {
    stopTimer();
}

void PlaceholderEditor::updateStatus() {
    switch (processor_.generationStatus()) {
    case GenerationStatus::Generating:
        statusLabel_.setText("Generating...", juce::dontSendNotification);
        break;
    case GenerationStatus::Done:
        statusLabel_.setText("Done: plays from the next bar", juce::dontSendNotification);
        break;
    case GenerationStatus::NoResult:
        statusLabel_.setText("No result: the slot is unchanged", juce::dontSendNotification);
        break;
    case GenerationStatus::Idle:
        statusLabel_.setText({}, juce::dontSendNotification);
        break;
    }
}

void PlaceholderEditor::timerCallback() {
    updateStatus();
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
    auto area = getLocalBounds();
    dragHandle_->setBounds(area.removeFromBottom(56).reduced(16));
    auto top = area.removeFromTop(48).reduced(16, 10);
    generateButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(8);
    styleBox_.setBounds(top);
    statusLabel_.setBounds(area.removeFromTop(28).reduced(16, 0));
}

} // namespace mm::plugin
