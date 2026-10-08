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

    const auto settings = processor_.instanceSettings();
    roleBox_.addItem("Solo", 1);
    roleBox_.addItem("Hub", 2);
    roleBox_.addItem("Voice", 3);
    roleBox_.setSelectedId(settings.role == mm::core::InstanceRole::Hub     ? 2
                           : settings.role == mm::core::InstanceRole::Voice ? 3
                                                                            : 1,
                           juce::dontSendNotification);
    roleBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        const int id = roleBox_.getSelectedId();
        next.role = id == 2   ? mm::core::InstanceRole::Hub
                    : id == 3 ? mm::core::InstanceRole::Voice
                              : mm::core::InstanceRole::Solo;
        processor_.setInstanceSettings(next);
        updateStatus();
    };
    addAndMakeVisible(roleBox_);

    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        voiceBox_.addItem("Voice " + juce::String(voice), voice);
    }
    voiceBox_.setSelectedId(settings.outputVoice, juce::dontSendNotification);
    voiceBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.outputVoice = voiceBox_.getSelectedId();
        processor_.setInstanceSettings(next);
    };
    addAndMakeVisible(voiceBox_);

    followBox_.addItem("Slot: follows hub", 1);
    followBox_.addItem("Slot: own", 2);
    followBox_.setSelectedId(settings.slotFollow == mm::core::SlotFollow::Own ? 2 : 1, juce::dontSendNotification);
    followBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.slotFollow = followBox_.getSelectedId() == 2 ? mm::core::SlotFollow::Own : mm::core::SlotFollow::Hub;
        processor_.setInstanceSettings(next);
    };
    addChildComponent(followBox_);

    takeOverButton_.onClick = [this] { processor_.acceptHubOffer(); };
    addChildComponent(takeOverButton_);

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

    setSize(600, 360);
    processor_.midiExporter().requestExport(processor_.lastKnownBpm());
    updateStatus();
    startTimerHz(4); // keep the exported tempo equal to the DAW tempo and the status line current
}

PlaceholderEditor::~PlaceholderEditor() {
    stopTimer();
}

void PlaceholderEditor::updateStatus() {
    using mm::core::GroupStatus;
    juce::String group;
    switch (processor_.groupStatus()) {
    case GroupStatus::Hub: {
        const auto voices = processor_.groupVoices();
        group = "Hub: " + juce::String(static_cast<int>(voices)) + (voices == 1 ? " voice" : " voices");
        break;
    }
    case GroupStatus::VoiceConnected:
        group = "Connected to the hub";
        break;
    case GroupStatus::HubMissing:
        group = "Hub not found";
        break;
    case GroupStatus::HubOffered:
        group = "Hub not found: you can take over";
        break;
    case GroupStatus::HubRefused:
        group = "There is already a hub";
        break;
    case GroupStatus::Solo:
        break;
    }
    takeOverButton_.setVisible(processor_.groupStatus() == GroupStatus::HubOffered);
    const auto role = processor_.instanceSettings().role;
    const int roleId = role == mm::core::InstanceRole::Hub ? 2 : role == mm::core::InstanceRole::Voice ? 3 : 1;
    if (roleBox_.getSelectedId() != roleId) {
        roleBox_.setSelectedId(roleId, juce::dontSendNotification); // e.g. a voice that took over the hub
    }
    const bool isVoice = role == mm::core::InstanceRole::Voice;
    generateButton_.setEnabled(!isVoice);
    followBox_.setVisible(isVoice);
    if (isVoice && processor_.lateSwitches() > 0) {
        group += " (" + juce::String(static_cast<int>(processor_.lateSwitches())) + " late)";
    }
    groupText_ = group;

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
    case GenerationStatus::UseHub:
        statusLabel_.setText("A voice plays what the hub generates", juce::dontSendNotification);
        break;
    case GenerationStatus::Idle:
        statusLabel_.setText({}, juce::dontSendNotification);
        break;
    }
    if (groupText_.isNotEmpty()) {
        const auto generation = statusLabel_.getText();
        statusLabel_.setText(generation.isEmpty() ? groupText_ : groupText_ + "  |  " + generation,
                             juce::dontSendNotification);
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
    auto roles = area.removeFromTop(48).reduced(16, 10);
    takeOverButton_.setBounds(roles.removeFromRight(120));
    roles.removeFromRight(8);
    followBox_.setBounds(roles.removeFromRight(150));
    roles.removeFromRight(8);
    voiceBox_.setBounds(roles.removeFromRight(120));
    roles.removeFromRight(8);
    roleBox_.setBounds(roles);
    auto top = area.removeFromTop(48).reduced(16, 10);
    generateButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(8);
    styleBox_.setBounds(top);
    statusLabel_.setBounds(area.removeFromTop(28).reduced(16, 0));
}

} // namespace mm::plugin
