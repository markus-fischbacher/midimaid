#include "plugin/EditorParts.h"

#include "core/OutputStage.h"
#include "core/ParameterRegister.h"
#include "core/TextKeys.h"
#include "plugin/EmbeddedGrooves.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

void DragHandle::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff23252d));
    g.fillRoundedRectangle(bounds, 7.0f);
    g.setColour(juce::Colour(0xff3a3e4a));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);

    // A page with a folded corner and a downward arrow: "take the file along". With room, the label follows.
    const bool labelled = getWidth() >= 120;
    const float iconWidth = 18.0f;
    const auto label = tr(mm::core::text::kDragLabel);
    juce::Font font{juce::FontOptions(13.0f)};
    const float labelWidth = labelled ? juce::GlyphArrangement::getStringWidth(font, label) : 0.0f;
    const float gap = labelled ? 8.0f : 0.0f;
    const float left = bounds.getCentreX() - (iconWidth + gap + labelWidth) / 2.0f;
    const float top = bounds.getCentreY() - 10.0f;
    juce::Path page;
    page.startNewSubPath(left, top);
    page.lineTo(left + 11.0f, top);
    page.lineTo(left + iconWidth - 2.0f, top + 7.0f);
    page.lineTo(left + iconWidth - 2.0f, top + 20.0f);
    page.lineTo(left, top + 20.0f);
    page.closeSubPath();
    g.setColour(juce::Colour(0xff4fc3a1));
    g.strokePath(page, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    juce::Path fold;
    fold.startNewSubPath(left + 11.0f, top);
    fold.lineTo(left + 11.0f, top + 7.0f);
    fold.lineTo(left + iconWidth - 2.0f, top + 7.0f);
    g.strokePath(fold, juce::PathStrokeType(1.2f));
    juce::Path arrow;
    const float ax = left + 8.0f;
    arrow.startNewSubPath(ax, top + 9.0f);
    arrow.lineTo(ax, top + 16.0f);
    arrow.startNewSubPath(ax - 3.0f, top + 13.0f);
    arrow.lineTo(ax, top + 16.5f);
    arrow.lineTo(ax + 3.0f, top + 13.0f);
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (labelled) {
        g.setColour(juce::Colour(0xffe8eaf0));
        g.setFont(font);
        g.drawText(label, juce::Rectangle<float>(left + iconWidth + gap, bounds.getY(), labelWidth + 2.0f, bounds.getHeight()),
                   juce::Justification::centredLeft, false);
    }
}

juce::File DragHandle::file() const {
    return voice_ == 0 ? exporter_.readyFile() : exporter_.readyFile(voice_);
}

ScaleCorner::ScaleCorner() {
    setComponentID("scaleCorner");
    setMouseCursor(juce::MouseCursor::BottomRightCornerResizeCursor);
}

void ScaleCorner::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff9ba1af).withAlpha(0.7f));
    for (int i = 0; i < 3; ++i) {
        const float offset = 4.0f + static_cast<float>(i) * 4.0f;
        g.drawLine(bounds.getRight() - offset - 1.0f, bounds.getBottom() - 2.0f, bounds.getRight() - 2.0f,
                   bounds.getBottom() - offset - 1.0f, 1.2f);
    }
}

void ScaleCorner::mouseDown(const juce::MouseEvent& event) {
    startScreenX_ = event.getScreenX();
    if (onDragStart) {
        onDragStart();
    }
}

void ScaleCorner::mouseDrag(const juce::MouseEvent& event) {
    if (onDrag) {
        onDrag(static_cast<double>(event.getScreenX() - startScreenX_));
    }
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

juce::File firstMidiFile(const juce::StringArray& files) {
    for (const auto& path : files) {
        const juce::File file(path);
        const auto extension = file.getFileExtension().toLowerCase();
        if (extension == ".mid" || extension == ".midi") {
            return file;
        }
    }
    return {};
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
    lockButton_.addMouseListener(this, false); // a right click opens the menu of the dimensions

    focusButton_.setComponentID("focus_" + juce::String(voice));
    focusButton_.setButtonText(tr(mm::core::text::kRollFocus));
    focusButton_.onClick = [this] {
        if (onFocusClicked) {
            onFocusClicked(voice_);
        }
    };
    addAndMakeVisible(focusButton_);

    renewButton_.setComponentID("renew_" + juce::String(voice));
    renewButton_.setButtonText(tr(mm::core::text::kButtonRenew));
    renewButton_.onClick = [this] {
        if (slotIndex_ >= 0) {
            processor_.renewVoice(static_cast<size_t>(voice_ - 1), std::nullopt, static_cast<size_t>(slotIndex_));
        }
        if (onAction) {
            onAction();
        }
    };
    addAndMakeVisible(renewButton_);

    varyButton_.setComponentID("vary_" + juce::String(voice));
    varyButton_.setButtonText(tr(mm::core::text::kButtonVary));
    varyButton_.onClick = [this] {
        if (onVaryClicked) {
            onVaryClicked(voice_);
        }
    };
    addAndMakeVisible(varyButton_);

    // The groove of the voice: a template (SPEC 3.9, v1.1) and the swing control (off for a template with swing of its own)
    grooveBox_.setComponentID("groove_" + juce::String(voice));
    grooveBox_.onChange = [this] {
        if (updatingGroove_ || slotIndex_ < 0) {
            return;
        }
        const int id = grooveBox_.getSelectedId();
        std::string choice = mm::core::kGrooveStraight;
        if (id == 2) {
            choice = mm::core::kGrooveDrumReference;
        } else if (id >= 10) {
            const auto& templates = embeddedGrooves().templates();
            if (static_cast<size_t>(id - 10) < templates.size()) {
                choice = templates[static_cast<size_t>(id - 10)].id;
            }
        }
        processor_.setVoiceGroove(static_cast<size_t>(slotIndex_), static_cast<size_t>(voice_ - 1), choice, std::nullopt);
        if (onAction) {
            onAction();
        }
    };
    addAndMakeVisible(grooveBox_);
    swingSlider_.setComponentID("swing_" + juce::String(voice));
    swingSlider_.setRange(50.0, 75.0, 1.0);
    swingSlider_.setValue(50.0, juce::dontSendNotification);
    swingSlider_.setTextValueSuffix(tr(mm::core::text::kSwingSuffix));
    swingSlider_.onValueChange = [this] {
        if (updatingGroove_ || slotIndex_ < 0) {
            return;
        }
        // one drag of the slider is one undo step: the first change begins it, the others continue it
        const bool continues = swingSlider_.isMouseButtonDown() && !swingDragStart_;
        swingDragStart_ = false;
        processor_.setVoiceGroove(static_cast<size_t>(slotIndex_), static_cast<size_t>(voice_ - 1), std::nullopt,
                                  static_cast<float>(swingSlider_.getValue() / 100.0), continues);
    };
    swingSlider_.onDragStart = [this] { swingDragStart_ = true; };
    addAndMakeVisible(swingSlider_);

    duplicateButton_.setComponentID("duplicate_" + juce::String(voice));
    duplicateButton_.setButtonText(tr(mm::core::text::kButtonDuplicate));
    duplicateButton_.onClick = [this] {
        roll_.duplicateSelection();
        if (onAction) {
            onAction();
        }
    };
    addAndMakeVisible(duplicateButton_);
    updateButtons();

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
                         view->locked ? name + tr(mm::core::text::kVoiceLockedSuffix)
                         : (view->lockFlags.pitch || view->lockFlags.rhythm || view->lockFlags.velocity)
                             ? name + tr(mm::core::text::kVoicePartialLockedSuffix)
                             : name,
                         false);
    }
    roll_.setSource(view);
    const bool filled = view != nullptr && view->hasPattern;
    lockFlags_ = filled ? view->lockFlags : mm::core::LockFlags{};
    slotIndex_ = filled ? view->slot - 1 : -1;
    lockButton_.setEnabled(filled);
    lockButton_.setToggleState(filled && view->locked, juce::dontSendNotification);
    updateMuted();
    updateButtons();
    fillGroove(view);
}

juce::PopupMenu VoiceRow::lockMenu() const {
    const bool filled = slotIndex_ >= 0;
    juce::PopupMenu menu;
    menu.addItem(kLockMenuBase + 1, tr(mm::core::text::kLockPitch), filled, lockFlags_.pitch);
    menu.addItem(kLockMenuBase + 2, tr(mm::core::text::kLockRhythm), filled, lockFlags_.rhythm);
    menu.addItem(kLockMenuBase + 3, tr(mm::core::text::kLockVelocity), filled, lockFlags_.velocity);
    menu.addSeparator();
    menu.addItem(kLockMenuBase + 4, tr(mm::core::text::kLockAll), filled,
                 lockFlags_.pitch && lockFlags_.rhythm && lockFlags_.velocity);
    return menu;
}

void VoiceRow::runLockAction(int id) {
    if (slotIndex_ < 0 || id <= kLockMenuBase || id > kLockMenuBase + 4) {
        return;
    }
    const auto slot = static_cast<size_t>(slotIndex_);
    const auto voice = static_cast<size_t>(voice_ - 1);
    if (id == kLockMenuBase + 4) {
        const bool all = lockFlags_.pitch && lockFlags_.rhythm && lockFlags_.velocity;
        processor_.setVoiceLocked(slot, voice, !all);
        return;
    }
    const bool now = id == kLockMenuBase + 1 ? lockFlags_.pitch : id == kLockMenuBase + 2 ? lockFlags_.rhythm : lockFlags_.velocity;
    processor_.setVoiceLock(slot, voice,
                            id == kLockMenuBase + 1   ? mm::core::LockDimension::Pitch
                            : id == kLockMenuBase + 2 ? mm::core::LockDimension::Rhythm
                                                      : mm::core::LockDimension::Velocity,
                            !now);
}

void VoiceRow::mouseDown(const juce::MouseEvent& event) {
    if (event.originalComponent == &lockButton_ && event.mods.isPopupMenu() && lockButton_.isEnabled()) {
        lockMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&lockButton_),
                                 [this](int id) { runLockAction(id); });
    }
}

void VoiceRow::fillGroove(const ProcessorBase::VoiceView* view) {
    updatingGroove_ = true;
    const bool filled = view != nullptr && view->hasPattern;
    grooveBox_.clear(juce::dontSendNotification);
    grooveBox_.addItem(tr(mm::core::text::kGroovePickStraight), 1);
    if (filled && view->drumReference) {
        grooveBox_.addItem(tr(mm::core::text::kGroovePickDrumReference), 2);
    }
    int index = 0;
    for (const auto& groove : embeddedGrooves().templates()) {
        grooveBox_.addItem(juce::String::fromUTF8(localized(groove.nameDe, groove.nameEn).c_str()), 10 + index++);
    }
    int selected = 1;
    if (filled) {
        if (view->grooveId == mm::core::kGrooveDrumReference && view->drumReference) {
            selected = 2;
        } else {
            index = 0;
            for (const auto& groove : embeddedGrooves().templates()) {
                if (groove.id == view->grooveId) {
                    selected = 10 + index;
                }
                ++index;
            }
        }
    }
    grooveBox_.setSelectedId(selected, juce::dontSendNotification);
    grooveBox_.setEnabled(filled);
    swingSlider_.setValue(filled ? std::round(view->swing * 100.0f) : 50.0, juce::dontSendNotification);
    swingSlider_.setEnabled(filled && view->swingActive);
    updatingGroove_ = false;
}

void VoiceRow::updateButtons() {
    const bool usable = slotIndex_ >= 0 && !lockButton_.getToggleState();
    renewButton_.setEnabled(usable);
    varyButton_.setEnabled(usable);
    duplicateButton_.setEnabled(slotIndex_ >= 0 && !roll_.selection().empty());
}

void VoiceRow::updateMuted() {
    const auto* raw = processor_.parameters().getRawParameterValue(mm::core::muteParameterId(voice_));
    roll_.setDimmed(raw != nullptr && raw->load() >= 0.5f);
}

void VoiceRow::setCollapsed(bool collapsed) {
    if (collapsed_ == collapsed) {
        return;
    }
    collapsed_ = collapsed;
    roll_.setVisible(!collapsed);
    grooveBox_.setVisible(!collapsed);
    swingSlider_.setVisible(!collapsed);
    resized();
}

void VoiceRow::setFocused(bool focused) {
    focusButton_.setToggleState(focused, juce::dontSendNotification);
}

void VoiceRow::paintOverChildren(juce::Graphics& g) {
    if (dropHighlight_) {
        g.setColour(juce::Colour(0xff4fc3a1));
        g.drawRect(getLocalBounds(), 2);
    }
}

bool VoiceRow::isInterestedInFileDrag(const juce::StringArray& files) {
    return firstMidiFile(files) != juce::File();
}

void VoiceRow::fileDragEnter(const juce::StringArray&, int, int) {
    dropHighlight_ = true;
    repaint();
}

void VoiceRow::fileDragExit(const juce::StringArray&) {
    dropHighlight_ = false;
    repaint();
}

void VoiceRow::filesDropped(const juce::StringArray& files, int, int) {
    dropHighlight_ = false;
    repaint();
    if (const auto file = firstMidiFile(files); file != juce::File() && onMidiDropped) {
        onMidiDropped(voice_, file);
    }
}

void VoiceRow::resized() {
    auto area = getLocalBounds().reduced(16, 4);
    if (collapsed_) {
        // A strip: everything but the roll, side by side.
        nameLabel_.setBounds(area.removeFromLeft(100));
        muteButton_.setBounds(area.removeFromLeft(80));
        lockButton_.setBounds(area.removeFromLeft(90));
        focusButton_.setBounds(area.removeFromLeft(70));
        renewButton_.setBounds(area.removeFromLeft(56).reduced(0, 2));
        area.removeFromLeft(6);
        varyButton_.setBounds(area.removeFromLeft(90).reduced(0, 2));
        area.removeFromLeft(6);
        duplicateButton_.setBounds(area.removeFromLeft(100).reduced(0, 2));
        area.removeFromLeft(10);
        drag_.setBounds(area.removeFromLeft(60).withHeight(area.getHeight()));
        roll_.setBounds({});
        grooveBox_.setBounds({});
        swingSlider_.setBounds({});
        return;
    }
    auto left = area.removeFromLeft(160);
    nameLabel_.setBounds(left.removeFromTop(28));
    // Two buttons side by side per line: Stumm | Sperren, Neu | Variation, Fokus | Duplizieren.
    const auto pair = [&left](juce::Component& first, juce::Component& second) {
        auto line = left.removeFromTop(28);
        first.setBounds(line.removeFromLeft(line.getWidth() / 2).reduced(0, 1));
        second.setBounds(line.reduced(2, 1));
    };
    pair(muteButton_, lockButton_);
    pair(renewButton_, varyButton_);
    pair(focusButton_, duplicateButton_);
    drag_.setBounds(left.removeFromBottom(40));
    area.removeFromLeft(8);
    roll_.setBounds(area);
    // The groove controls sit in the title strip of the roll, at its right end (the name stands at the left): the left
    // column has no room for two more lines in a window with two voices.
    auto strip = area.removeFromTop(EditableRoll::kTitleHeight).reduced(4, 1);
    swingSlider_.setBounds(strip.removeFromRight(130));
    strip.removeFromRight(6);
    grooveBox_.setBounds(strip.removeFromRight(150));
}

SlotButton::SlotButton(int slot) : slot_(slot) {}

juce::var SlotButton::describe(int slot) {
    return juce::var("mm-slot:" + juce::String(slot));
}

int SlotButton::slotOf(const juce::var& description) {
    const auto text = description.toString();
    if (!text.startsWith("mm-slot:")) {
        return 0;
    }
    const auto number = text.fromFirstOccurrenceOf(":", false, false);
    if (!number.containsOnly("0123456789") || number.isEmpty() || number.length() > 2) {
        return 0;
    }
    const int slot = number.getIntValue();
    return slot >= 1 && slot <= static_cast<int>(mm::core::kSlotCount) ? slot : 0;
}

void SlotButton::mouseDown(const juce::MouseEvent& event) {
    menuAsked_ = event.mods.isPopupMenu();
    if (menuAsked_) { // a right click is no click on the button
        if (onMenu) {
            onMenu(slot_, event.getScreenPosition());
        }
        return;
    }
    juce::TextButton::mouseDown(event);
}

void SlotButton::mouseDrag(const juce::MouseEvent& event) {
    juce::TextButton::mouseDrag(event);
    if (menuAsked_ || event.getDistanceFromDragStart() < 8) {
        return;
    }
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        container != nullptr && !container->isDragAndDropActive()) {
        container->startDragging(describe(slot_), this);
    }
}

void SlotButton::paintOverChildren(juce::Graphics& g) {
    if (dropTarget_) {
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 2.0f);
        if (copyHeld()) { // the drop copies: a plus in the corner says so
            const auto badge = getLocalBounds().toFloat().removeFromTop(14.0f).removeFromRight(14.0f);
            g.setColour(juce::Colour(0xff4fc3a1));
            g.fillEllipse(badge);
            g.setColour(juce::Colours::black);
            g.drawLine(badge.getCentreX() - 3.5f, badge.getCentreY(), badge.getCentreX() + 3.5f, badge.getCentreY(), 1.6f);
            g.drawLine(badge.getCentreX(), badge.getCentreY() - 3.5f, badge.getCentreX(), badge.getCentreY() + 3.5f, 1.6f);
        }
    }
}

bool SlotButton::copyHeld() {
    return juce::ComponentPeer::getCurrentModifiersRealtime().isAltDown();
}

void SlotButton::itemDragMove(const SourceDetails&) {
    repaint(); // the plus follows the Alt key
}

void SlotButton::timerCallback() {
    const bool held = copyHeld();
    if (held != copyShown_) {
        copyShown_ = held;
        repaint();
        juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
    }
}

bool SlotButton::showsCopy(const juce::var& description, int slot, bool altHeld) {
    const int from = slotOf(description);
    return altHeld && from != 0 && from != slot;
}

juce::MouseCursor SlotButton::getMouseCursor() {
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
        container != nullptr && container->isDragAndDropActive() &&
        showsCopy(container->getCurrentDragDescription(), slot_, copyHeld())) {
        return juce::MouseCursor::CopyingCursor;
    }
    return juce::TextButton::getMouseCursor();
}

bool SlotButton::isInterestedInDragSource(const SourceDetails& details) {
    const int from = slotOf(details.description);
    return from != 0 && from != slot_;
}

void SlotButton::itemDragEnter(const SourceDetails&) {
    dropTarget_ = true;
    copyShown_ = copyHeld();
    startTimer(30);
    repaint();
}

void SlotButton::itemDragExit(const SourceDetails&) {
    stopTimer();
    dropTarget_ = false;
    repaint();
}

void SlotButton::itemDropped(const SourceDetails& details) {
    stopTimer();
    dropTarget_ = false;
    repaint();
    const int from = slotOf(details.description);
    if (from != 0 && from != slot_ && onSlotDropped) {
        onSlotDropped(from, slot_, copyHeld());
    }
}

} // namespace mm::plugin
