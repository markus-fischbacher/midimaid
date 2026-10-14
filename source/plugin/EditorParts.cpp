#include "plugin/EditorParts.h"

#include "core/OutputStage.h"
#include "core/ParameterRegister.h"
#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"
#include "core/Shortcuts.h"
#include "plugin/MidiMaidLookAndFeel.h"
#include "plugin/UserGrooveStore.h"

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
    addAndMakeVisible(focusButton_); // always in reach: it is what the user works with most (D-253)

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
    addChildComponent(varyButton_);

    // The groove of the voice: a template (SPEC 3.9, v1.1) and the swing control (off for a template with swing of its own)
    grooveBox_.setComponentID("groove_" + juce::String(voice));
    grooveBox_.onChange = [this] {
        if (updatingGroove_ || slotIndex_ < 0) {
            return;
        }
        const int id = grooveBox_.getSelectedId();
        std::string choice = mm::core::kGrooveStraight;
        if (id == 3) {
            askGrooveName();
            updatingGroove_ = true; // back to what the voice has until the name is given
            grooveBox_.setSelectedId(grooveSelection_, juce::dontSendNotification);
            updatingGroove_ = false;
            return;
        }
        if (id == 2) {
            choice = mm::core::kGrooveDrumReference;
        } else if (id >= 10) {
            const auto grooves = userGrooves().library();
            const auto& templates = grooves->templates();
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
    flamHint_.setComponentID("flamHint_" + juce::String(voice));
    flamHint_.setText(tr(mm::core::text::kSwingFlamHint), juce::dontSendNotification);
    flamHint_.setFont(juce::FontOptions(12.0f));
    flamHint_.setColour(juce::Label::textColourId, juce::Colour(0xffe0a040));
    flamHint_.setJustificationType(juce::Justification::centredRight);
    flamHint_.setInterceptsMouseClicks(false, false);
    flamHint_.setVisible(false);
    addChildComponent(flamHint_);

    duplicateButton_.setComponentID("duplicate_" + juce::String(voice));
    duplicateButton_.setButtonText(tr(mm::core::text::kButtonDuplicate));
    duplicateButton_.onClick = [this] {
        roll_.duplicateSelection();
        if (onAction) {
            onAction();
        }
    };
    addChildComponent(duplicateButton_);
    moreButton_.setComponentID("more_" + juce::String(voice));
    moreButton_.setButtonText("...");
    moreButton_.onClick = [this] {
        juce::PopupMenu menu;
        menu.addItem(1, tr(mm::core::text::kButtonVary), varyButton_.isEnabled());
        menu.addItem(2, tr(mm::core::text::kButtonDuplicate), duplicateButton_.isEnabled());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&moreButton_),
                           [safe = juce::Component::SafePointer<VoiceRow>(this)](int result) {
                               if (safe == nullptr) {
                                   return;
                               }
                               if (result == 1) {
                                   safe->varyButton_.triggerClick();
                               } else if (result == 2) {
                                   safe->duplicateButton_.triggerClick();
                               }
                           });
    };
    addAndMakeVisible(moreButton_);
    updateButtons();

    roll_.setComponentID("roll_" + juce::String(voice));
    addAndMakeVisible(roll_);
    drag_.setComponentID("drag_" + juce::String(voice));
    addAndMakeVisible(drag_);
}

juce::Colour roleColour(mm::core::VoiceRole role) {
    switch (role) {
    case mm::core::VoiceRole::Bass:
        return juce::Colour(0xff4fc3a1);
    case mm::core::VoiceRole::Melody:
        return juce::Colour(0xffe0a458);
    case mm::core::VoiceRole::Stabs:
        return juce::Colour(0xffe07a7a);
    case mm::core::VoiceRole::Arp:
        return juce::Colour(0xff7a9be0);
    case mm::core::VoiceRole::Counter:
        return juce::Colour(0xffb78ae0);
    }
    return juce::Colour(0xffe0a458);
}

namespace {
constexpr int kDotSize = 22;
constexpr int kDotGap = 8;
} // namespace

void VoiceBar::setVoices(std::vector<mm::core::VoiceRole> roles, int focused) {
    if (roles == roles_ && focused == focused_) {
        return;
    }
    roles_ = std::move(roles);
    focused_ = focused;
    repaint();
}

juce::Rectangle<int> VoiceBar::dotBounds(size_t index) const {
    return {static_cast<int>(index) * (kDotSize + kDotGap), (getHeight() - kDotSize) / 2, kDotSize, kDotSize};
}

int VoiceBar::dotAt(int x) const {
    for (int i = 0; i < mm::core::kMaxVoices; ++i) {
        const auto bounds = dotBounds(static_cast<size_t>(i));
        if (x >= bounds.getX() - kDotGap / 2 && x < bounds.getRight() + kDotGap / 2) {
            return i;
        }
    }
    return -1;
}

void VoiceBar::paint(juce::Graphics& g) {
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    for (size_t i = 0; i < static_cast<size_t>(mm::core::kMaxVoices); ++i) {
        const auto bounds = dotBounds(i).toFloat();
        const bool filled = i < roles_.size();
        if (filled) {
            g.setColour(roleColour(roles_[i]));
            g.fillEllipse(bounds);
            g.setColour(juce::Colours::black);
        } else {
            g.setColour(juce::Colours::white.withAlpha(0.25f));
            g.drawEllipse(bounds.reduced(1.0f), 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.35f));
        }
        g.drawText(juce::String(static_cast<int>(i) + 1), bounds.toNearestInt(), juce::Justification::centred);
        if (filled && focused_ == static_cast<int>(i) + 1) {
            g.setColour(juce::Colours::white);
            g.drawEllipse(bounds.expanded(3.0f), 2.0f);
        }
    }
}

void VoiceBar::mouseUp(const juce::MouseEvent& event) {
    if (!getLocalBounds().contains(event.getPosition()) || event.mouseWasDraggedSinceMouseDown()) {
        return;
    }
    const int dot = dotAt(event.x);
    if (dot < 0) {
        return;
    }
    if (static_cast<size_t>(dot) < roles_.size()) {
        if (onJump) {
            onJump(static_cast<size_t>(dot));
        }
    } else if (onAdd) {
        onAdd();
    }
}

void VoiceRow::setView(const ProcessorBase::VoiceView* view, const juce::String& fallbackName, bool fallbackMelody) {
    const auto name = view != nullptr && view->displayName.isNotEmpty() ? view->displayName : fallbackName;
    nameLabel_.setText(name, juce::dontSendNotification);
    const bool melody = view != nullptr ? view->melody : fallbackMelody;
    roll_.setAccent(view != nullptr && view->hasPattern ? roleColour(view->role)
                    : melody                           ? roleColour(mm::core::VoiceRole::Melody)
                                                       : roleColour(mm::core::VoiceRole::Bass));
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

bool VoiceRow::saveGrooveAs(const juce::String& name) {
    if (slotIndex_ < 0) {
        return false;
    }
    const bool saved = !processor_
                            .saveVoiceGrooveAsTemplate(static_cast<size_t>(slotIndex_), static_cast<size_t>(voice_ - 1),
                                                       name.trim().toStdString())
                            .empty();
    if (saved && onAction) {
        onAction();
    }
    return saved;
}

void VoiceRow::askGrooveName() {
    auto* window = new juce::AlertWindow(tr(mm::core::text::kGrooveSaveTitle), tr(mm::core::text::kGrooveSaveHint),
                                         juce::MessageBoxIconType::NoIcon, this);
    window->setComponentID("grooveName");
    window->addTextEditor("name", {});
    window->addButton(tr(mm::core::text::kGrooveSaveButton), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(tr(mm::core::text::kReferencesCancel), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    const auto alive = alive_;
    window->enterModalState(true, juce::ModalCallbackFunction::create([this, alive, window](int result) {
                                if (result == 1 && *alive) {
                                    saveGrooveAs(window->getTextEditorContents("name"));
                                }
                            }),
                            true);
}

VoiceRow::~VoiceRow() {
    *alive_ = false;
}

void VoiceRow::fillGroove(const ProcessorBase::VoiceView* view) {
    updatingGroove_ = true;
    const bool filled = view != nullptr && view->hasPattern;
    grooveBox_.clear(juce::dontSendNotification);
    grooveBox_.addItem(tr(mm::core::text::kGroovePickStraight), 1);
    if (filled && view->drumReference) {
        grooveBox_.addItem(tr(mm::core::text::kGroovePickDrumReference), 2);
    }
    if (filled && view->canSaveGroove) {
        grooveBox_.addItem(tr(mm::core::text::kGroovePickSave), 3);
    }
    const auto grooves = userGrooves().library();
    int index = 0;
    for (const auto& groove : grooves->templates()) {
        grooveBox_.addItem(juce::String::fromUTF8(localized(groove.nameDe, groove.nameEn).c_str()), 10 + index++);
    }
    int selected = 1;
    if (filled) {
        if (view->grooveId == mm::core::kGrooveDrumReference && view->drumReference) {
            selected = 2;
        } else {
            index = 0;
            for (const auto& groove : grooves->templates()) {
                if (groove.id == view->grooveId) {
                    selected = 10 + index;
                }
                ++index;
            }
        }
    }
    grooveSelection_ = selected;
    grooveBox_.setSelectedId(selected, juce::dontSendNotification);
    grooveBox_.setEnabled(filled);
    swingSlider_.setValue(filled ? std::round(view->swing * 100.0f) : 50.0, juce::dontSendNotification);
    swingSlider_.setEnabled(filled && view->swingActive);
    flam_ = filled && view->swingFlam;
    flamHint_.setVisible(flam_ && !collapsed_);
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
    flamHint_.setVisible(flam_ && !collapsed);
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

CommandPalette::CommandPalette() : list_("commands", this) {
    setComponentID("palette");
    search_.setComponentID("palette_search");
    search_.setTextToShowWhenEmpty(tr(mm::core::text::kPaletteSearch), juce::Colours::white.withAlpha(0.4f));
    search_.addListener(this);
    addAndMakeVisible(search_);
    list_.setComponentID("palette_list");
    list_.setRowHeight(28);
    list_.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(list_);
    setWantsKeyboardFocus(false);
    setVisible(false);
}

void CommandPalette::open() {
    search_.clear();
    refill();
    setVisible(true);
    toFront(true);
    search_.grabKeyboardFocus();
}

void CommandPalette::close() {
    if (!isVisible()) {
        return;
    }
    setVisible(false);
    if (auto* parent = getParentComponent()) {
        parent->grabKeyboardFocus();
    }
}

void CommandPalette::setSearch(const juce::String& text) {
    search_.setText(text, juce::dontSendNotification);
    refill();
}

void CommandPalette::refill() {
    listed_.clear();
    names_.clear();
    const auto needle = search_.getText().trim().toLowerCase();
    for (const auto& action : mm::core::shortcutActions()) {
        if (action.id == "palette") {
            continue;
        }
        const auto name = tr(action.textKey);
        if (needle.isEmpty() || name.toLowerCase().contains(needle)) {
            listed_.emplace_back(action.id);
            names_.push_back(name);
        }
    }
    list_.updateContent();
    list_.repaint();
    if (!listed_.empty()) {
        list_.selectRow(0);
    }
}

void CommandPalette::moveSelection(int delta) {
    if (listed_.empty()) {
        return;
    }
    const int row = std::clamp(list_.getSelectedRow() + delta, 0, static_cast<int>(listed_.size()) - 1);
    list_.selectRow(row);
}

bool CommandPalette::runSelected() {
    const int row = list_.getSelectedRow();
    if (row < 0 || static_cast<size_t>(row) >= listed_.size()) {
        return false;
    }
    const auto action = listed_[static_cast<size_t>(row)];
    close();
    if (onRun) {
        onRun(action);
    }
    return true;
}

void CommandPalette::paint(juce::Graphics& g) {
    g.setColour(juce::Colour(MidiMaidLookAndFeel::kSurface));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    if (listed_.empty()) {
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawText(tr(mm::core::text::kPaletteEmpty), list_.getBounds(), juce::Justification::centred);
    }
}

void CommandPalette::resized() {
    auto area = getLocalBounds().reduced(10);
    search_.setBounds(area.removeFromTop(32));
    area.removeFromTop(6);
    list_.setBounds(area);
}

bool CommandPalette::keyPressed(const juce::KeyPress& press) {
    if (press == juce::KeyPress::escapeKey) {
        close();
        return true;
    }
    if (press == juce::KeyPress::downKey || press == juce::KeyPress::upKey) {
        moveSelection(press == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }
    return false;
}

void CommandPalette::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || static_cast<size_t>(row) >= names_.size()) {
        return;
    }
    if (selected) {
        g.setColour(juce::Colour(MidiMaidLookAndFeel::kAccent).withAlpha(0.25f));
        g.fillRoundedRectangle(0.0f, 1.0f, static_cast<float>(width), static_cast<float>(height - 2), 4.0f);
    }
    g.setColour(juce::Colours::white.withAlpha(selected ? 1.0f : 0.8f));
    g.drawText(names_[static_cast<size_t>(row)], 10, 0, width - 20, height, juce::Justification::centredLeft);
}

void CommandPalette::listBoxItemClicked(int row, const juce::MouseEvent&) {
    list_.selectRow(row);
    runSelected();
}

void VoiceRow::resized() {
    // A lane: the strip on the left (name, the actions of every voice, the grip) and the roll; the rarer actions
    // (Variation, Duplicate) are in the menu of the strip, Focus stays outside (D-252, D-253).
    auto area = getLocalBounds().reduced(12, 2);
    constexpr int kStripWidth = 180;
    if (collapsed_) {
        auto strip = area.removeFromLeft(kCollapsedStripWidth);
        nameLabel_.setBounds(strip.removeFromLeft(80));
        muteButton_.setBounds(strip.removeFromLeft(60).reduced(0, 4));
        lockButton_.setBounds(strip.removeFromLeft(70).reduced(0, 4));
        renewButton_.setBounds(strip.removeFromLeft(44).reduced(2, 4));
        focusButton_.setBounds(strip.removeFromLeft(64).reduced(2, 4));
        moreButton_.setBounds(strip.removeFromLeft(32).reduced(2, 4));
        drag_.setBounds(area.removeFromLeft(60).withHeight(area.getHeight()));
        roll_.setBounds({});
        grooveBox_.setBounds({});
        swingSlider_.setBounds({});
        flamHint_.setBounds({});
        return;
    }
    auto left = area.removeFromLeft(kStripWidth).reduced(8, 6);
    auto head = left.removeFromTop(26);
    moreButton_.setBounds(head.removeFromRight(32));
    nameLabel_.setBounds(head);
    left.removeFromTop(4);
    auto actions = left.removeFromTop(28);
    muteButton_.setBounds(actions.removeFromLeft(actions.getWidth() / 2));
    lockButton_.setBounds(actions);
    left.removeFromTop(4);
    actions = left.removeFromTop(28);
    renewButton_.setBounds(actions.removeFromLeft(actions.getWidth() / 2));
    focusButton_.setBounds(actions);
    drag_.setBounds(left.removeFromBottom(40));
    roll_.setBounds(area);
    // The groove controls sit in the title strip of the roll, at its right end (the name stands at the left).
    auto strip = area.removeFromTop(EditableRoll::kTitleHeight).reduced(4, 1);
    swingSlider_.setBounds(strip.removeFromRight(130));
    strip.removeFromRight(6);
    grooveBox_.setBounds(strip.removeFromRight(150));
    strip.removeFromRight(8);
    flamHint_.setBounds(strip.removeFromRight(210));
}

void VoiceRow::paint(juce::Graphics& g) {
    // Flat: the strip is a tonal surface, the roll paints its own.
    auto area = getLocalBounds().reduced(12, 2);
    g.setColour(juce::Colour(MidiMaidLookAndFeel::kSurface));
    g.fillRoundedRectangle(area.removeFromLeft(collapsed_ ? kCollapsedStripWidth : 180).toFloat(), 6.0f);
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

GroupPanel::GroupPanel(ProcessorBase& processor) {
    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        auto button = std::make_unique<juce::ToggleButton>();
        button->setComponentID("group_mute_" + juce::String(voice));
        button->setButtonText({});
        button->setTooltip(tr(mm::core::text::kGroupMuteTip));
        addChildComponent(*button);
        if (auto* parameter = processor.parameters().getParameter(mm::core::muteParameterId(voice))) {
            attachments_.push_back(std::make_unique<juce::ButtonParameterAttachment>(*parameter, *button));
        }
        muteButtons_.push_back(std::move(button));
    }
}

void GroupPanel::setContent(std::vector<Row> rows, juce::String message, juce::StringArray warnings,
                            juce::String allFine) {
    const auto same = [](const Row& a, const Row& b) {
        return a.role == b.role && a.name == b.name && a.assigned == b.assigned && a.follow == b.follow && a.warn == b.warn;
    };
    if (message == message_ && warnings == warnings_ && allFine == allFine_ &&
        std::equal(rows.begin(), rows.end(), rows_.begin(), rows_.end(), same)) {
        return; // nothing to show anew (the editor asks several times a second)
    }
    rows_ = std::move(rows);
    message_ = std::move(message);
    warnings_ = std::move(warnings);
    allFine_ = std::move(allFine);
    for (size_t i = 0; i < muteButtons_.size(); ++i) {
        muteButtons_[i]->setVisible(isVisible() && message_.isEmpty() && i < rows_.size());
    }
    resized();
    repaint();
}

void GroupPanel::resized() {
    for (size_t i = 0; i < muteButtons_.size(); ++i) {
        muteButtons_[i]->setVisible(isVisible() && message_.isEmpty() && i < rows_.size());
        muteButtons_[i]->setBounds(getLocalBounds().withHeight(kRowHeight).translated(0, static_cast<int>(i) * kRowHeight)
                                       .removeFromLeft(26));
    }
}

void GroupPanel::paint(juce::Graphics& g) {
    g.setFont(13.0f);
    if (!message_.isEmpty()) {
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawFittedText(message_, getLocalBounds().withHeight(2 * kRowHeight), juce::Justification::topLeft, 3);
        return;
    }
    int y = 0;
    for (const auto& row : rows_) {
        auto line = juce::Rectangle<int>(0, y, getWidth(), kRowHeight);
        line.removeFromLeft(28); // the mute switch
        g.setColour(roleColour(row.role));
        g.fillEllipse(line.removeFromLeft(14).toFloat().withSizeKeepingCentre(10.0f, 10.0f));
        line.removeFromLeft(6);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.drawText(row.name, line.removeFromLeft(78), juce::Justification::centredLeft);
        g.setColour(row.warn ? juce::Colour(0xffe5645a) : juce::Colours::white.withAlpha(0.7f));
        const int assignedWidth = row.follow.isEmpty() ? line.getWidth() : line.getWidth() / 2;
        g.drawText(row.assigned, line.removeFromLeft(assignedWidth), juce::Justification::centredLeft);
        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.drawText(row.follow, line, juce::Justification::centredLeft);
        y += kRowHeight;
    }
    auto rest = getLocalBounds().withTrimmedTop(y + 10);
    if (warnings_.isEmpty()) {
        g.setColour(juce::Colour(0xff4fc3a1));
        g.drawText(allFine_, rest.removeFromTop(kRowHeight), juce::Justification::centredLeft);
        return;
    }
    g.setColour(juce::Colour(0xffe5645a));
    for (const auto& warning : warnings_) {
        g.drawFittedText(warning, rest.removeFromTop(2 * 16), juce::Justification::topLeft, 2);
    }
}

} // namespace mm::plugin
