#include "plugin/EditorParts.h"

#include "core/ParameterRegister.h"
#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

void DragHandle::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff2c2c33));
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    for (int row = -1; row <= 1; ++row) {
        for (int col = -2; col <= 2; ++col) {
            g.fillEllipse(cx + static_cast<float>(col) * 8.0f - 1.5f, cy + static_cast<float>(row) * 8.0f - 1.5f, 3.0f,
                          3.0f);
        }
    }
}

juce::File DragHandle::file() const {
    return voice_ == 0 ? exporter_.readyFile() : exporter_.readyFile(voice_);
}

void DragHandle::mouseDrag(const juce::MouseEvent& event) {
    if (dragging_ || event.getDistanceFromDragStart() < 4) {
        return;
    }
    const auto dragged = file();
    if (dragged == juce::File()) {
        return; // the export is still running
    }
    dragging_ = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles({dragged.getFullPathName()}, false, this,
                                                               [this] { dragging_ = false; });
}

VoiceRow::VoiceRow(ProcessorBase& processor, int voice)
    : processor_(processor), voice_(voice), roll_(processor, voice), drag_(processor.midiExporter(), voice) {
    setComponentID("row_" + juce::String(voice));
    nameLabel_.setComponentID("name_" + juce::String(voice));
    nameLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.85f));
    addAndMakeVisible(nameLabel_);

    muteButton_.setComponentID("mute_" + juce::String(voice));
    if (auto* parameter = processor.parameters().getParameter(mm::core::muteParameterId(voice))) {
        muteAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*parameter, muteButton_);
    }
    muteButton_.setButtonText(tr(mm::core::text::kButtonMute));
    addAndMakeVisible(muteButton_);

    lockButton_.setComponentID("lock_" + juce::String(voice));
    lockButton_.setButtonText(tr(mm::core::text::kButtonLock));
    lockButton_.setEnabled(false);
    lockButton_.onClick = [this] {
        if (slotIndex_ >= 0) {
            processor_.setVoiceLocked(static_cast<size_t>(slotIndex_), static_cast<size_t>(voice_ - 1),
                                      lockButton_.getToggleState());
        }
    };
    addAndMakeVisible(lockButton_);

    roll_.setComponentID("roll_" + juce::String(voice));
    addAndMakeVisible(roll_);
    drag_.setComponentID("drag_" + juce::String(voice));
    addAndMakeVisible(drag_);
}

void VoiceRow::setView(const ProcessorBase::VoiceView* view, const juce::String& fallbackName, bool fallbackMelody) {
    const auto name = view != nullptr && view->displayName.isNotEmpty() ? view->displayName : fallbackName;
    nameLabel_.setText(name, juce::dontSendNotification);
    const bool melody = view != nullptr ? view->melody : fallbackMelody;
    roll_.setAccent(melody ? juce::Colour(0xffe0a458) : juce::Colour(0xff4fc3a1));
    if (view == nullptr || !view->hasPattern) {
        roll_.setContent(mm::core::layoutRoll({}, mm::core::kTicksPerQuarter * 4), {}, true);
    } else {
        roll_.setContent(mm::core::layoutRoll(view->notes, view->lengthTicks),
                         view->locked ? name + tr(mm::core::text::kVoiceLockedSuffix) : name, false);
    }
    roll_.setSource(view);
    const bool filled = view != nullptr && view->hasPattern;
    slotIndex_ = filled ? view->slot - 1 : -1;
    lockButton_.setEnabled(filled);
    lockButton_.setToggleState(filled && view->locked, juce::dontSendNotification);
    updateMuted();
}

void VoiceRow::updateMuted() {
    const auto* raw = processor_.parameters().getRawParameterValue(mm::core::muteParameterId(voice_));
    roll_.setDimmed(raw != nullptr && raw->load() >= 0.5f);
}

void VoiceRow::resized() {
    auto area = getLocalBounds().reduced(16, 4);
    auto left = area.removeFromLeft(130);
    nameLabel_.setBounds(left.removeFromTop(28));
    muteButton_.setBounds(left.removeFromTop(28));
    lockButton_.setBounds(left.removeFromTop(28));
    drag_.setBounds(left.removeFromBottom(40));
    area.removeFromLeft(8);
    roll_.setBounds(area);
}

} // namespace mm::plugin
