#include "plugin/PlaceholderEditor.h"

#include "plugin/KeyChords.h"

#include "core/ParameterRegister.h"
#include "core/TextKeys.h"
#include "plugin/AiText.h"
#include "plugin/AiUsage.h"
#include "plugin/EmbeddedTranslation.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/HelpDialog.h"
#include "plugin/HostDetection.h"
#include "plugin/GroupText.h"
#include "plugin/LibraryDialog.h"
#include "plugin/ReferencesDialog.h"
#include "plugin/SettingsDialog.h"

namespace mm::plugin {

namespace {

constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

namespace keys = mm::core::text;

/// The name of a scale in the language of the UI; an id without a translation shows as it is.
juce::String scaleLabel(std::string_view id) {
    return tr(keys::scaleKey(id));
}

juce::String barsText(uint32_t bars) {
    return tr(bars == 1 ? keys::kBarsOne : keys::kBarsMany, {{"n", std::to_string(bars)}});
}

} // namespace

PlaceholderEditor::PlaceholderEditor(ProcessorBase& processor)
    : juce::AudioProcessorEditor(processor), processor_(processor),
      dragHandle_(std::make_unique<DragHandle>(processor.midiExporter())) {
    setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(*dragHandle_);
    setWantsKeyboardFocus(true); // the keys the user assigned (SPEC 8.3, D-235); a click on a roll lands here

    const auto settings = processor_.instanceSettings();
    roleBox_.setComponentID("role");
    voiceBox_.setComponentID("voice");
    followBox_.setComponentID("follow");
    takeOverButton_.setComponentID("takeOver");
    dragHandle_->setComponentID("drag");
    roleBox_.addItem(tr(keys::kRoleSolo), 1);
    roleBox_.addItem(tr(keys::kRoleHub), 2);
    roleBox_.addItem(tr(keys::kRoleVoice), 3);
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
        voiceBox_.addItem(tr(keys::kVoiceN, {{"n", std::to_string(voice)}}), voice);
    }
    voiceBox_.setSelectedId(settings.outputVoice, juce::dontSendNotification);
    voiceBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.outputVoice = voiceBox_.getSelectedId();
        processor_.setInstanceSettings(next);
    };
    addAndMakeVisible(voiceBox_);

    followBox_.addItem(tr(keys::kFollowHub), 1);
    followBox_.addItem(tr(keys::kFollowOwn), 2);
    followBox_.setSelectedId(settings.slotFollow == mm::core::SlotFollow::Own ? 2 : 1, juce::dontSendNotification);
    followBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.slotFollow = followBox_.getSelectedId() == 2 ? mm::core::SlotFollow::Own : mm::core::SlotFollow::Hub;
        processor_.setInstanceSettings(next);
    };
    addChildComponent(followBox_);

    outputBox_.addItem(tr(keys::kOutputVoice), 1);
    outputBox_.addItem(tr(keys::kOutputNone), 2);
    outputBox_.setSelectedId(settings.outputMode == mm::core::OutputMode::None ? 2 : 1, juce::dontSendNotification);
    outputBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.outputMode = outputBox_.getSelectedId() == 2 ? mm::core::OutputMode::None : mm::core::OutputMode::OneVoice;
        processor_.setInstanceSettings(next);
    };
    addChildComponent(outputBox_);

    takeOverButton_.setButtonText(tr(keys::kButtonTakeOver));
    takeOverButton_.onClick = [this] { processor_.acceptHubOffer(); };
    addChildComponent(takeOverButton_);

    const auto& profiles = processor_.styles().profiles();
    const auto current = processor_.instanceSettings().generation.styleId;
    for (size_t i = 0; i < profiles.size(); ++i) {
        styleBox_.addItem(juce::String::fromUTF8(localized(profiles[i].nameDe, profiles[i].nameEn).c_str()), static_cast<int>(i) + 1);
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

    styleBox_.setComponentID("style");

    referenceSetBox_.setComponentID("referenceSet");
    referenceSetBox_.onChange = [this] {
        const auto index = static_cast<size_t>(referenceSetBox_.getSelectedId() - 1);
        if (index < referenceSetIds_.size()) {
            referenceStore().setActiveSet(referenceStyleId(), referenceSetIds_[index]);
        }
    };
    addAndMakeVisible(referenceSetBox_);
    refreshReferenceSets();

    keyBox_.setComponentID("key");
    keyBox_.addItem(tr(keys::kKeyAuto), 1);
    for (int i = 0; i < 12; ++i) {
        keyBox_.addItem(tr(keys::kKeyNote, {{"note", kKeyNames[i]}}), i + 2);
    }
    keyBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        const int id = keyBox_.getSelectedId();
        if (id <= 1) {
            next.generation.root.reset();
        } else {
            next.generation.root = static_cast<mm::core::PitchClass>(id - 2);
            globalSettings().update(
                [&](mm::core::GlobalSettings& global) { global.startRoot = *next.generation.root; });
        }
        processor_.setInstanceSettings(next);
        correctSlotKey(next);
    };
    addChildComponent(keyBox_);

    gridBox_.setComponentID("grid_box");
    for (const int division : {4, 8, 16, 32}) {
        gridBox_.addItem("1/" + juce::String(division), division);
    }
    gridBox_.setSelectedId(16, juce::dontSendNotification);
    gridBox_.onChange = [this] {
        applyEditSettings();
        saveEditSettings();
    };
    addChildComponent(gridBox_);
    tripletButton_.setComponentID("triplet");
    tripletButton_.setButtonText(tr(keys::kRollTriplet));
    tripletButton_.onClick = [this] {
        applyEditSettings();
        saveEditSettings();
    };
    addChildComponent(tripletButton_);
    snapBox_.setComponentID("snap_box");
    snapBox_.addItem(tr(keys::kRollSnapScale), 1);
    snapBox_.addItem(tr(keys::kRollSnapChromatic), 2);
    snapBox_.setSelectedId(1, juce::dontSendNotification);
    snapBox_.onChange = [this] {
        applyEditSettings();
        saveEditSettings();
    };
    addChildComponent(snapBox_);

    expertButton_.setComponentID("expert");
    expertButton_.setButtonText(tr(keys::kButtonExpert));
    expertButton_.onClick = [this] {
        expert_ = expertButton_.getToggleState();
        globalSettings().update([this](mm::core::GlobalSettings& global) { global.expert = expert_; });
        applyLevel();
    };
    addChildComponent(expertButton_);
    varyAllButton_.setComponentID("vary_all");
    varyAllButton_.setButtonText(tr(keys::kButtonVaryAll));
    varyAllButton_.onClick = [this] { varyVoice(std::nullopt, std::nullopt); };
    addChildComponent(varyAllButton_);
    voiceBar_.setComponentID("voice_bar");
    voiceBar_.onJump = [this](size_t index) { setFocusedVoice(static_cast<int>(index) + 1); };
    voiceBar_.onAdd = [this] { showAddVoiceMenu(); };
    addChildComponent(voiceBar_);
    addVoiceButton_.setComponentID("add_voice");
    addVoiceButton_.setButtonText(tr(keys::kButtonAddVoice));
    addVoiceButton_.onClick = [this] { showAddVoiceMenu(); };
    addChildComponent(addVoiceButton_);
    removeVoiceButton_.setComponentID("remove_voice");
    removeVoiceButton_.setButtonText(tr(keys::kButtonRemoveVoice));
    removeVoiceButton_.onClick = [this] {
        processor_.removeLastVoice();
        updateStatus();
        updateFullView();
    };
    addChildComponent(removeVoiceButton_);
    strengthLabel_.setText(tr(keys::kLabelStrength), juce::dontSendNotification);
    strengthLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(strengthLabel_);
    strengthSlider_.setComponentID("strength");
    strengthSlider_.setRange(0.0, 100.0, 1.0);
    strengthSlider_.setValue(30.0, juce::dontSendNotification);
    strengthSlider_.onValueChange = [this] {
        globalSettings().update(
            [this](mm::core::GlobalSettings& global) { global.variationStrength = variationStrength(); });
    };
    addChildComponent(strengthSlider_);

    undoButton_.setComponentID("undo");
    undoButton_.setButtonText(tr(keys::kButtonUndo));
    previewButton_.setComponentID("preview");
    previewButton_.setButtonText(tr(keys::kButtonPreview));
    previewButton_.onClick = [this] {
        processor_.setPreview(!processor_.previewing());
        updateActionButtons();
    };
    addChildComponent(previewButton_);
    undoButton_.onClick = [this] {
        processor_.undo();
        updateStatus();
    };
    addChildComponent(undoButton_);
    redoButton_.setComponentID("redo");
    redoButton_.setButtonText(tr(keys::kButtonRedo));
    redoButton_.onClick = [this] {
        processor_.redo();
        updateStatus();
    };
    addChildComponent(redoButton_);
    drumsButton_.setComponentID("drums_remove");
    drumsButton_.setButtonText(tr(keys::kButtonDrumsRemove));
    drumsButton_.onClick = [this] {
        processor_.removeDrumReference(static_cast<size_t>(std::max(shownInfo_.slot, 1) - 1));
        updateStatus();
    };
    addChildComponent(drumsButton_);
    historyBackButton_.setComponentID("history_back");
    historyBackButton_.setButtonText(tr(keys::kButtonHistoryBack));
    historyBackButton_.onClick = [this] {
        processor_.historyBack(static_cast<size_t>(std::max(shownInfo_.slot, 1) - 1));
        updateStatus();
    };
    addChildComponent(historyBackButton_);
    historyForwardButton_.setComponentID("history_forward");
    historyForwardButton_.setButtonText(tr(keys::kButtonHistoryForward));
    historyForwardButton_.onClick = [this] {
        processor_.historyForward(static_cast<size_t>(std::max(shownInfo_.slot, 1) - 1));
        updateStatus();
    };
    addChildComponent(historyForwardButton_);
    historyLabel_.setComponentID("history_label");
    historyLabel_.setJustificationType(juce::Justification::centred);
    historyLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(historyLabel_);

    scaleBox_.setComponentID("scale");
    scaleBox_.addItem(tr(keys::kScaleAuto), 1);
    {
        int id = 2;
        for (const auto& scale : mm::core::allScales()) {
            scaleBox_.addItem(tr(keys::kScaleItem, {{"name", scaleLabel(scale.id).toStdString()}}), id++);
        }
    }
    scaleBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        const auto scales = mm::core::allScales();
        const auto index = static_cast<size_t>(scaleBox_.getSelectedId() - 2);
        if (scaleBox_.getSelectedId() <= 1 || index >= scales.size()) {
            next.generation.scaleId.reset();
        } else {
            next.generation.scaleId = std::string(scales[index].id);
            globalSettings().update(
                [&](mm::core::GlobalSettings& global) { global.startScaleId = *next.generation.scaleId; });
        }
        processor_.setInstanceSettings(next);
        correctSlotKey(next);
    };
    addChildComponent(scaleBox_);

    barsBox_.setComponentID("bars");
    for (const int bars : {1, 2, 4, 8, 16}) {
        barsBox_.addItem(barsText(static_cast<uint32_t>(bars)), bars);
    }
    barsBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.generation.lengthBars = static_cast<uint32_t>(barsBox_.getSelectedId());
        processor_.setInstanceSettings(next);
    };
    addChildComponent(barsBox_);

    promptEditor_.setJustification(juce::Justification::centredLeft);
    promptEditor_.setComponentID("prompt");
    promptEditor_.setMultiLine(false);
    promptEditor_.setInputRestrictions(static_cast<int>(mm::core::kMaxPromptBytes));
    promptEditor_.setTextToShowWhenEmpty(tr(keys::kPromptHint), juce::Colours::white.withAlpha(0.45f));
    promptEditor_.onTextChange = [this] {
        auto next = processor_.instanceSettings();
        next.generation.prompt = promptEditor_.getText().toStdString();
        processor_.setInstanceSettings(next);
    };
    promptEditor_.onReturnKey = [this] {
        processor_.generate();
        updateStatus();
    };
    addChildComponent(promptEditor_);

    refineEditor_.setJustification(juce::Justification::centredLeft);
    refineEditor_.setComponentID("refine_text");
    refineEditor_.setMultiLine(false);
    refineEditor_.setInputRestrictions(static_cast<int>(mm::core::kMaxPromptBytes));
    refineEditor_.setTextToShowWhenEmpty(tr(keys::kRefineHint), juce::Colours::white.withAlpha(0.45f));
    refineEditor_.onReturnKey = [this] { startRefine(); };
    addChildComponent(refineEditor_);
    refineScopeBox_.setComponentID("refine_scope");
    refineScopeBox_.addItem(tr(keys::kRefineScopeAll), 1);
    refineScopeBox_.setSelectedId(1, juce::dontSendNotification);
    addChildComponent(refineScopeBox_);
    refineButton_.setButtonText(tr(keys::kButtonRefine));
    refineButton_.setComponentID("refine");
    refineButton_.onClick = [this] { startRefine(); };
    addChildComponent(refineButton_);

    settingsButton_.setButtonText(tr(keys::kButtonSettings));
    settingsButton_.setComponentID("settings");
    settingsButton_.onClick = [] { SettingsDialog::show(); };
    addChildComponent(settingsButton_);
    referencesButton_.setButtonText(tr(keys::kButtonReferences));
    referencesButton_.setComponentID("references");
    referencesButton_.onClick = [this] {
        const auto id = processor_.instanceSettings().generation.styleId;
        const auto* style = processor_.styles().findOrFallback(id);
        ReferencesDialog::show(style != nullptr ? style->id : id,
                               style != nullptr ? juce::String::fromUTF8(localized(style->nameDe, style->nameEn).c_str()) : juce::String(id));
    };
    addChildComponent(referencesButton_);
    libraryButton_.setButtonText(tr(keys::kButtonLibrary));
    libraryButton_.setComponentID("library");
    libraryButton_.onClick = [this] { LibraryDialog::show(processor_, processor_.libraryAliveFlag()); };
    addChildComponent(libraryButton_);
    errorSettingsButton_.setButtonText(tr(keys::kButtonSettings));
    errorSettingsButton_.setComponentID("errorSettings");
    errorSettingsButton_.onClick = [] { SettingsDialog::show(); };
    addChildComponent(errorSettingsButton_);

    retryButton_.setButtonText(tr(keys::kButtonRetry));
    retryButton_.setComponentID("retry");
    retryButton_.onClick = [this] {
        processor_.retryGeneration();
        updateStatus();
    };
    addChildComponent(retryButton_);
    offlineButton_.setButtonText(tr(keys::kButtonOffline));
    offlineButton_.setComponentID("offline");
    offlineButton_.onClick = [this] {
        processor_.generateOffline();
        updateStatus();
    };
    addChildComponent(offlineButton_);
    autoOfflineButton_.setButtonText(tr(keys::kToggleAutoOffline));
    autoOfflineButton_.setComponentID("autoOffline");
    autoOfflineButton_.onClick = [this] { processor_.setAutoOffline(autoOfflineButton_.getToggleState()); };
    addChildComponent(autoOfflineButton_);

    seedEditor_.setJustification(juce::Justification::centredLeft);
    seedEditor_.setComponentID("seed");
    seedEditor_.setInputRestrictions(20, "0123456789");
    seedEditor_.setTextToShowWhenEmpty(tr(keys::kSeedRandom), juce::Colours::white.withAlpha(0.45f));
    const auto applySeed = [this] {
        auto next = processor_.instanceSettings();
        next.generation.seed = mm::core::parseSeed(seedEditor_.getText().toStdString());
        processor_.setInstanceSettings(next);
        syncFields(processor_.instanceSettings()); // a seed that does not fit goes back to "random"
    };
    seedEditor_.onReturnKey = applySeed;
    seedEditor_.onFocusLost = applySeed;
    addChildComponent(seedEditor_);

    randomButton_.setButtonText(tr(keys::kButtonRandom));
    randomButton_.setComponentID("random");
    randomButton_.onClick = [this] {
        auto next = processor_.instanceSettings();
        next.generation.seed.reset();
        processor_.setInstanceSettings(next);
        syncFields(processor_.instanceSettings());
    };
    addChildComponent(randomButton_);

    for (auto* slider : {&energySlider_, &creativitySlider_}) {
        slider->setRange(0.0, 100.0, 1.0);
        addChildComponent(*slider);
    }
    energySlider_.setComponentID("energy");
    creativitySlider_.setComponentID("creativity");
    energySlider_.onValueChange = [this] {
        auto next = processor_.instanceSettings();
        next.generation.energyPct = static_cast<int>(energySlider_.getValue());
        processor_.setInstanceSettings(next);
    };
    creativitySlider_.onValueChange = [this] {
        auto next = processor_.instanceSettings();
        next.generation.creativityPct = static_cast<int>(creativitySlider_.getValue());
        processor_.setInstanceSettings(next);
    };
    // The chromatic share (SPEC 3.8, expert level): -1 is "the style's", 0 to 100 the share of notes that may leave the scale.
    chromaticSlider_.setComponentID("chromatic");
    chromaticSlider_.setRange(-1.0, 100.0, 1.0);
    chromaticSlider_.setValue(-1.0, juce::dontSendNotification);
    chromaticSlider_.textFromValueFunction = [](double value) {
        return value < 0.0 ? tr(keys::kChromaticStyle) : juce::String(static_cast<int>(value)) + " %";
    };
    chromaticSlider_.onValueChange = [this] {
        auto next = processor_.instanceSettings();
        const int value = static_cast<int>(chromaticSlider_.getValue());
        next.generation.chromaticPct = value < 0 ? std::nullopt : std::optional<int>(value);
        processor_.setInstanceSettings(next);
    };
    addChildComponent(chromaticSlider_);
    // How strongly the reference set acts (SPEC 3.20, expert level, 50 % by default).
    personalSlider_.setComponentID("personal");
    personalSlider_.setRange(0.0, 100.0, 1.0);
    personalSlider_.setValue(50.0, juce::dontSendNotification);
    personalSlider_.textFromValueFunction = [](double value) { return juce::String(static_cast<int>(value)) + " %"; };
    personalSlider_.onValueChange = [this] {
        auto next = processor_.instanceSettings();
        next.generation.personalPct = static_cast<int>(personalSlider_.getValue());
        processor_.setInstanceSettings(next);
    };
    addChildComponent(personalSlider_);
    startBox_.setComponentID("startMode");
    startBox_.addItem(tr(keys::kStartRestart), 1);
    startBox_.addItem(tr(keys::kStartLegato), 2);
    startBox_.setSelectedId(1, juce::dontSendNotification);
    startBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.legato = startBox_.getSelectedId() == 2;
        processor_.setInstanceSettings(next);
    };
    addChildComponent(startBox_);
    quantBox_.setComponentID("switchQuant");
    for (const std::string_view key : {keys::kQuantBar, keys::kQuantBeat, keys::kQuantBars2, keys::kQuantBars4, keys::kQuantBars8,
                            keys::kQuantBars16}) {
        quantBox_.addItem(tr(key), quantBox_.getNumItems() + 1);
    }
    quantBox_.setSelectedId(1, juce::dontSendNotification);
    quantBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.quantize = mm::core::switchQuantizeFromInt(quantBox_.getSelectedId() - 1);
        processor_.setInstanceSettings(next);
    };
    addChildComponent(quantBox_);
    hatModeBox_.setComponentID("hatMode");
    hatModeBox_.addItem(tr(keys::kHatModeFree), 1);
    hatModeBox_.addItem(tr(keys::kHatModeWith), 2);
    hatModeBox_.addItem(tr(keys::kHatModeAgainst), 3);
    hatModeBox_.setSelectedId(1, juce::dontSendNotification);
    hatModeBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        const int id = hatModeBox_.getSelectedId();
        next.generation.hatMode = id == 2   ? mm::core::HatMode::With
                                  : id == 3 ? mm::core::HatMode::Against
                                            : mm::core::HatMode::Free;
        processor_.setInstanceSettings(next);
    };
    addChildComponent(hatModeBox_);
    hatModeLabel_.setText(tr(keys::kLabelHatMode), juce::dontSendNotification);
    hatModeLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(hatModeLabel_);
    chromaticLabel_.setText(tr(keys::kLabelChromatic), juce::dontSendNotification);
    chromaticLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(chromaticLabel_);
    personalLabel_.setText(tr(keys::kLabelPersonal), juce::dontSendNotification);
    personalLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(personalLabel_);
    energyLabel_.setText(tr(keys::kLabelEnergy), juce::dontSendNotification);
    creativityLabel_.setText(tr(keys::kLabelCreativity), juce::dontSendNotification);
    for (auto* label : {&energyLabel_, &creativityLabel_}) {
        label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        addChildComponent(*label);
    }

    for (size_t i = 0; i < mm::core::kSlotCount; ++i) {
        auto& button = *slotButtons_.emplace_back(std::make_unique<SlotButton>(static_cast<int>(i) + 1));
        button.setButtonText(juce::String(static_cast<int>(i) + 1));
        button.setComponentID("slot" + juce::String(static_cast<int>(i) + 1));
        button.setClickingTogglesState(false);
        button.onClick = [this, i] { processor_.selectSlot(static_cast<int>(i) + 1); };
        button.onMenu = [this](int slot, juce::Point<int> where) { showSlotMenu(slot, where); };
        button.onSlotDropped = [this](int from, int to, bool copy) {
            if (copy) {
                processor_.copySlotTo(static_cast<size_t>(from - 1), static_cast<size_t>(to - 1));
            } else {
                processor_.swapSlots(static_cast<size_t>(from - 1), static_cast<size_t>(to - 1));
            }
            updateStatus();
        };
        addChildComponent(button);
    }

    infoLabel_.setComponentID("info");
    infoLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    addChildComponent(infoLabel_);

    ratingBox_.setComponentID("rating");
    ratingBox_.addItem(tr(keys::kLibraryNoRating), 1);
    for (int stars = 1; stars <= 5; ++stars) {
        juce::String text;
        for (int i = 0; i < 5; ++i) {
            text += juce::String::fromUTF8(i < stars ? "\xe2\x98\x85" : "\xe2\x98\x86");
        }
        ratingBox_.addItem(text, stars + 1);
    }
    ratingBox_.setSelectedId(1, juce::dontSendNotification);
    ratingBox_.onChange = [this] {
        if (ratingUpdating_) {
            return;
        }
        processor_.rateSlot(static_cast<size_t>(std::max(processor_.playingSlotInfo().slot, 1) - 1),
                            std::max(ratingBox_.getSelectedId() - 1, 0)); // a rating is one click
        updateStatus();
    };
    addChildComponent(ratingBox_);

    openHubButton_.setButtonText(tr(keys::kButtonOpenHub));
    openHubButton_.setComponentID("openHub");
    openHubButton_.onClick = [this] { processor_.showHub(); };
    addChildComponent(openHubButton_);

    muteButton_.setButtonText(tr(keys::kButtonMute));
    muteButton_.setComponentID("mute");
    addChildComponent(muteButton_);

    octaveBox_.setComponentID("octave");
    for (int octave = -2; octave <= 2; ++octave) {
        octaveBox_.addItem(tr(keys::kOctaveItem, {{"value", (octave > 0 ? "+" : "") + std::to_string(octave)}}),
                           octave + 3);
    }
    octaveBox_.setSelectedId(settings.octave + 3, juce::dontSendNotification);
    octaveBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.octave = octaveBox_.getSelectedId() - 3;
        processor_.setInstanceSettings(next);
        updateVoiceView();
    };
    addChildComponent(octaveBox_);

    zoomBox_.setComponentID("zoom");
    for (const int step : mm::core::kUiScaleSteps) {
        zoomBox_.addItem(tr(keys::kZoomItem, {{"value", std::to_string(step)}}), step);
    }
    syncZoomBox();
    zoomBox_.onChange = [this] {
        processor_.setUiScalePct(zoomBox_.getSelectedId());
        applyScale();
    };
    addAndMakeVisible(zoomBox_);

    scaleCorner_.onDragStart = [this] {
        cornerStartPct_ = processor_.uiScalePct();
        cornerStartWidth_ = static_cast<double>(getWidth()) * cornerStartPct_ / 100.0; // the width on screen
    };
    scaleCorner_.onDrag = [this](double dragX) {
        const int pct = mm::core::uiScaleForDrag(cornerStartPct_, cornerStartWidth_, dragX);
        if (pct != processor_.uiScalePct()) {
            processor_.setUiScalePct(pct);
            applyScale();
        }
    };
    addAndMakeVisible(scaleCorner_);

    helpButton_.setButtonText(tr(keys::kButtonHelp));
    helpButton_.setComponentID("help");
    helpButton_.onClick = [this] { HelpDialog::show(detectHostCapabilities(processor_).host); };
    addAndMakeVisible(helpButton_);
    followButton_.setButtonText(tr(keys::kButtonFollow));
    followButton_.setComponentID("follow_playhead");
    followButton_.setClickingTogglesState(true);
    followButton_.setToggleState(processor_.followPlayhead(), juce::dontSendNotification);
    followButton_.onClick = [this] { processor_.setFollowPlayhead(followButton_.getToggleState()); };
    addAndMakeVisible(followButton_);

    evolveButton_.setButtonText(tr(keys::kButtonEvolve));
    evolveButton_.setComponentID("evolve");
    evolveButton_.setClickingTogglesState(true);
    evolveButton_.onClick = [this] {
        processor_.setEvolve(evolveButton_.getToggleState());
        updateStatus();
    };
    addChildComponent(evolveButton_);
    evolveBackButton_.setButtonText(tr(keys::kButtonHistoryBack));
    evolveBackButton_.setComponentID("evolve_back");
    evolveBackButton_.onClick = [this] { processor_.evolveBack(); };
    addChildComponent(evolveBackButton_);
    evolveStageLabel_.setComponentID("evolve_stage");
    evolveStageLabel_.setJustificationType(juce::Justification::centred);
    evolveStageLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(evolveStageLabel_);
    evolveKeepButton_.setButtonText(tr(keys::kButtonEvolveKeep));
    evolveKeepButton_.setComponentID("evolve_keep");
    evolveKeepButton_.onClick = [this] {
        processor_.evolveKeep();
        updateStatus();
    };
    addChildComponent(evolveKeepButton_);
    evolveEveryBox_.setComponentID("evolve_every");
    for (int n = mm::core::kEvolveMinPasses; n <= mm::core::kEvolveMaxPasses; ++n) {
        evolveEveryBox_.addItem(tr(keys::kLabelEvolveEvery, {{"n", std::to_string(n)}}), n);
    }
    evolveEveryBox_.setSelectedId(processor_.instanceSettings().evolve.everyPasses, juce::dontSendNotification);
    evolveEveryBox_.onChange = [this] { applyEvolveSettings(); };
    addChildComponent(evolveEveryBox_);
    evolveOriginalButton_.setButtonText(tr(keys::kButtonEvolveFromOriginal));
    evolveOriginalButton_.setComponentID("evolve_original");
    evolveOriginalButton_.setToggleState(processor_.instanceSettings().evolve.fromOriginal, juce::dontSendNotification);
    evolveOriginalButton_.onClick = [this] { applyEvolveSettings(); };
    addChildComponent(evolveOriginalButton_);
    evolveStrengthLabel_.setText(tr(keys::kLabelEvolveStrength), juce::dontSendNotification);
    evolveStrengthLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    addChildComponent(evolveStrengthLabel_);
    evolveStrengthSlider_.setComponentID("evolve_strength");
    evolveStrengthSlider_.setRange(1.0, 100.0, 1.0);
    evolveStrengthSlider_.setValue(processor_.instanceSettings().evolve.strengthPct, juce::dontSendNotification);
    evolveStrengthSlider_.onValueChange = [this] { applyEvolveSettings(); };
    addChildComponent(evolveStrengthSlider_);

    rollView_.setComponentID("roll");
    addChildComponent(rollView_);

    generateButton_.setButtonText(tr(keys::kButtonGenerate));
    generateButton_.setComponentID("generate");
    generateButton_.onClick = [this] {
        if (processor_.generatingWithAi()) {
            processor_.cancelGeneration(); // the button is "Cancel" while the AI works (SPEC 7.9)
        } else {
            processor_.generate();
        }
        updateStatus();
    };
    // the main action in the accent colour
    generateButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(MidiMaidLookAndFeel::kAccent));
    generateButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::black);
    addAndMakeVisible(generateButton_);

    statusLabel_.setComponentID("status");
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(MidiMaidLookAndFeel::kTextDim));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setMinimumHorizontalScale(1.0f); // the hub hint is long: wrap instead of squeezing
    addAndMakeVisible(statusLabel_);

    commandsButton_.setComponentID("commands");
    commandsButton_.setButtonText(tr(keys::kButtonCommands));
    commandsButton_.onClick = [this] { openPalette(); };
    addChildComponent(commandsButton_);
    palette_.onRun = [this](const std::string& action) { runShortcut(action); };
    addChildComponent(palette_);
    inspectorButton_.setComponentID("inspector");
    inspectorButton_.setButtonText(tr(keys::kButtonInspector));
    inspectorButton_.setClickingTogglesState(true);
    inspectorButton_.onClick = [this] { setInspectorOpen(inspectorButton_.getToggleState()); };
    addChildComponent(inspectorButton_);
    groupPanel_.setComponentID("group_panel");
    addChildComponent(groupPanel_);
    const std::array<std::string_view, 5> tabKeys{keys::kInspectorInstance, keys::kSectionGenerator,
                                                  keys::kInspectorEdit, keys::kSectionEvolve, keys::kInspectorGroup};
    for (size_t i = 0; i < inspectorTabs_.size(); ++i) {
        auto& tab = inspectorTabs_[i];
        tab.setComponentID("inspector_tab_" + juce::String(static_cast<int>(i)));
        tab.setButtonText(tr(tabKeys[i]));
        tab.setRadioGroupId(701);
        tab.setClickingTogglesState(true);
        tab.setToggleState(static_cast<InspectorPage>(i) == inspectorPage_, juce::dontSendNotification);
        tab.onClick = [this, i] { setInspectorPage(static_cast<InspectorPage>(i)); };
        addChildComponent(tab);
    }
    modeButton_.setComponentID("dock_mode");
    modeButton_.onClick = [this] {
        refineMode_ = !refineMode_;
        applyPanels();
        resized();
    };
    addChildComponent(modeButton_);

    processor_.updateExport();
    applyStoredSettings();
    updateStatus();  // chooses the layout and the size
    playheadTicker_.tick = [this] { updatePlayhead(); };
    playheadTicker_.startTimerHz(30);
    startTimerHz(4); // keep the exported tempo equal to the DAW tempo and the status line current
}

PlaceholderEditor::~PlaceholderEditor() {
    setLookAndFeel(nullptr);
    playheadTicker_.stopTimer();
    stopTimer();
}

namespace {

/// What went wrong, in words for the status bar (SPEC 7.9).
juce::String aiFailureReason(const GenerationReport& report) {
    if (report.outcome == mm::ai::AiOutcome::Invalid) {
        return tr(keys::kAiReasonInvalid);
    }
    auto reason = aiStatusText(report.status, report.httpStatus);
    if (report.outcome == mm::ai::AiOutcome::ProviderError && report.error.size() > 0) {
        // what the provider said (never the key); the log has the full line
        auto detail = juce::String::fromUTF8(report.error.c_str());
        reason += ": " + (detail.length() > 140 ? detail.substring(0, 140) + "..." : detail);
    }
    return reason;
}

} // namespace

void PlaceholderEditor::updateStatus() {
    using mm::core::GroupStatus;
    juce::String group =
        juce::String::fromUTF8(groupStatusText(processor_.groupStatus(), processor_.groupVoices()).c_str());
    takeOverButton_.setVisible(processor_.groupStatus() == GroupStatus::HubOffered);
    const auto role = processor_.instanceSettings().role;
    const int roleId = role == mm::core::InstanceRole::Hub ? 2 : role == mm::core::InstanceRole::Voice ? 3 : 1;
    if (roleBox_.getSelectedId() != roleId) {
        roleBox_.setSelectedId(roleId, juce::dontSendNotification); // e.g. a voice that took over the hub
    }
    const bool isVoice = role == mm::core::InstanceRole::Voice;
    applyStoredSettings(); // until the file is read
    updateVoiceNames();
    if (followButton_.getToggleState() != processor_.followPlayhead()) { // e.g. a state that was loaded later
        followButton_.setToggleState(processor_.followPlayhead(), juce::dontSendNotification);
    }
    if (isVoice != voiceLayout_ || getWidth() == 0) {
        applyLayout(isVoice);
    }
    if (isVoice) {
        updateVoiceView();
    } else {
        updateFullView();
    }
    generateButton_.setEnabled(!isVoice || processor_.groupStatus() == GroupStatus::VoiceConnected);
    followBox_.setVisible(isVoice);
    openHubButton_.setEnabled(processor_.groupStatus() == GroupStatus::VoiceConnected);
    applyPanels();
    if (!voiceLayout_) {
        updateGroupPage();
    }
    if (role != mm::core::InstanceRole::Solo && processor_.otherFormatInProcess()) {
        group += tr(keys::kGroupMixedFormats);
    }
    if (isVoice && processor_.lateSwitches() > 0) {
        group += tr(keys::kStatusLate, {{"n", std::to_string(processor_.lateSwitches())}});
    }
    if (isVoice && processor_.effectiveTranspose() != 0) { // a voice shows the transposition of the group (SPEC 3.16)
        const int transposed = processor_.effectiveTranspose();
        group += "  |  " +
                 tr(keys::kSlotTransposed, {{"n", (transposed > 0 ? "+" : "") + std::to_string(transposed)}});
    }
    groupText_ = group;

    switch (processor_.generationStatus()) {
    case GenerationStatus::Generating:
        statusLabel_.setText(tr(processor_.generatingWithAi() ? keys::kStatusGeneratingAi : keys::kStatusGenerating),
                             juce::dontSendNotification);
        break;
    case GenerationStatus::Done:
    case GenerationStatus::DoneLocked:
    case GenerationStatus::DoneImportCut:
    case GenerationStatus::Refined: {
        // An AI result under the minimum of the style is shown and kept (SPEC 4.4): the rating is part of the message.
        const auto& report = processor_.lastReport();
        const auto status = processor_.generationStatus();
        const std::string_view done = status == GenerationStatus::Refined          ? keys::kStatusRefined
                                      : status == GenerationStatus::DoneImportCut  ? keys::kStatusDoneImportCut
                                      : status == GenerationStatus::DoneLocked     ? keys::kStatusDoneLocked
                                                                                   : keys::kStatusDone;
        juce::String text = report.usedAi && report.belowMinScore
                                ? tr(keys::kStatusBelowQuality, {{"score", std::to_string(report.score)}})
                                : tr(done);
        if (report.usedAi) { // the tokens and the estimated cost of the request and of the session (SPEC 7.5)
            const auto usage = usageStatusText(report.usage, AiUsage::instance().session(),
                                               AiUsage::instance().pricing().currency);
            if (!usage.empty()) {
                text += "  |  " + juce::String::fromUTF8(usage.c_str());
            }
        }
        statusLabel_.setText(text, juce::dontSendNotification);
        break;
    }
    case GenerationStatus::ImportRead:
    case GenerationStatus::ImportReadCut: {
        const auto& pending = processor_.pendingImport();
        const std::string notes = pending ? std::to_string(pending->plan.notes.size()) : "0";
        statusLabel_.setText(processor_.generationStatus() == GenerationStatus::ImportReadCut
                                 ? tr(keys::kStatusImportCut, {{"n", notes}})
                                 : tr(keys::kStatusImportRead,
                                      {{"n", notes}, {"m", pending ? std::to_string(pending->plan.lengthBars) : "0"}}),
                             juce::dontSendNotification);
        break;
    }
    case GenerationStatus::DrumRefApplied:
    case GenerationStatus::DrumRefStored: {
        const auto& ref = processor_.drumReference();
        statusLabel_.setText(processor_.generationStatus() == GenerationStatus::DrumRefStored
                                 ? tr(keys::kStatusDrumStored)
                                 : tr(keys::kStatusDrumApplied,
                                      {{"n", ref ? std::to_string(ref->kickSteps.count()) : "0"},
                                       {"m", ref ? std::to_string(ref->hatSteps.count()) : "0"}}),
                             juce::dontSendNotification);
        break;
    }
    case GenerationStatus::DrumRefNoKickHat:
        statusLabel_.setText(tr(keys::kStatusDrumNoKickHat), juce::dontSendNotification);
        break;
    case GenerationStatus::DrumRefRemoved:
        statusLabel_.setText(tr(keys::kStatusDrumRemoved), juce::dontSendNotification);
        break;
    case GenerationStatus::ImportApplied:
        statusLabel_.setText(tr(keys::kStatusImportApplied), juce::dontSendNotification);
        break;
    case GenerationStatus::LibrarySaved:
    case GenerationStatus::LibrarySetSaved:
    case GenerationStatus::LibraryNothingToSave:
    case GenerationStatus::LibraryFull:
    case GenerationStatus::LibraryLoaded:
    case GenerationStatus::LibrarySetLoaded:
    case GenerationStatus::LibraryLoadFailed:
    case GenerationStatus::LibraryImported:
    case GenerationStatus::LibraryExported:
    case GenerationStatus::LibraryExportFailed:
        statusLabel_.setText(tr(libraryStatusKey(processor_.generationStatus())), juce::dontSendNotification);
        break;
    case GenerationStatus::SlotCopied:
        statusLabel_.setText(tr(keys::kStatusSlotCopied), juce::dontSendNotification);
        break;
    case GenerationStatus::SlotPasted:
        statusLabel_.setText(tr(keys::kStatusSlotPasted), juce::dontSendNotification);
        break;
    case GenerationStatus::SlotNothingToCopy:
        statusLabel_.setText(tr(keys::kStatusSlotNothingToCopy), juce::dontSendNotification);
        break;
    case GenerationStatus::SlotCleared:
        statusLabel_.setText(tr(keys::kStatusSlotCleared), juce::dontSendNotification);
        break;
    case GenerationStatus::SlotsSwapped:
        statusLabel_.setText(tr(keys::kStatusSlotsSwapped), juce::dontSendNotification);
        break;
    case GenerationStatus::EvolveKept:
        statusLabel_.setText(tr(keys::kStatusEvolveKept), juce::dontSendNotification);
        break;
    case GenerationStatus::EvolveNothingToKeep:
        statusLabel_.setText(tr(keys::kStatusEvolveNothingToKeep), juce::dontSendNotification);
        break;
    case GenerationStatus::ImportNotFourFour:
        statusLabel_.setText(tr(keys::kStatusImportNotFourFour), juce::dontSendNotification);
        break;
    case GenerationStatus::ImportNoNotes:
        statusLabel_.setText(tr(keys::kStatusImportNoNotes), juce::dontSendNotification);
        break;
    case GenerationStatus::ImportUnreadable:
        statusLabel_.setText(tr(keys::kStatusImportUnreadable), juce::dontSendNotification);
        break;
    case GenerationStatus::AiFailed: {
        const std::string reason = aiFailureReason(processor_.lastReport()).toStdString();
        statusLabel_.setText(tr(keys::kStatusAiFailed, {{"reason", reason}}), juce::dontSendNotification);
        break;
    }
    case GenerationStatus::GenerationCancelled:
        statusLabel_.setText(tr(keys::kStatusCancelled), juce::dontSendNotification);
        break;
    case GenerationStatus::NothingToRefine:
        statusLabel_.setText(tr(keys::kStatusNothingToRefine), juce::dontSendNotification);
        break;
    case GenerationStatus::RefineAllLocked:
        statusLabel_.setText(tr(keys::kStatusRefineAllLocked), juce::dontSendNotification);
        break;
    case GenerationStatus::RefineNeedsAi:
        statusLabel_.setText(tr(keys::kStatusRefineNeedsAi), juce::dontSendNotification);
        break;
    case GenerationStatus::AllLocked:
        statusLabel_.setText(tr(keys::kStatusAllLocked), juce::dontSendNotification);
        break;
    case GenerationStatus::Varied:
        statusLabel_.setText(tr(keys::kStatusVaried, {{"n", std::to_string(processor_.lastVariationChanges())}}),
                             juce::dontSendNotification);
        break;
    case GenerationStatus::NothingToVary:
        statusLabel_.setText(tr(keys::kStatusNothingToVary), juce::dontSendNotification);
        break;
    case GenerationStatus::VaryAllLocked:
        statusLabel_.setText(tr(keys::kStatusVaryAllLocked), juce::dontSendNotification);
        break;
    case GenerationStatus::Renewed:
        statusLabel_.setText(tr(keys::kStatusRenewed), juce::dontSendNotification);
        break;
    case GenerationStatus::VoiceAdded:
        statusLabel_.setText(tr(keys::kStatusVoiceAdded), juce::dontSendNotification);
        break;
    case GenerationStatus::VoiceRemoved:
        statusLabel_.setText(tr(keys::kStatusVoiceRemoved), juce::dontSendNotification);
        break;
    case GenerationStatus::VoiceLimit:
        statusLabel_.setText(tr(keys::kStatusVoiceLimit), juce::dontSendNotification);
        break;
    case GenerationStatus::VoiceNoRemove:
        statusLabel_.setText(tr(keys::kStatusVoiceNoRemove), juce::dontSendNotification);
        break;
    case GenerationStatus::VoiceRemoveLocked:
        statusLabel_.setText(tr(keys::kStatusVoiceRemoveLocked), juce::dontSendNotification);
        break;
    case GenerationStatus::NothingToRenew:
        statusLabel_.setText(tr(keys::kStatusNothingToRenew), juce::dontSendNotification);
        break;
    case GenerationStatus::RenewLocked:
        statusLabel_.setText(tr(keys::kStatusRenewLocked), juce::dontSendNotification);
        break;
    case GenerationStatus::NoResult:
        statusLabel_.setText(tr(keys::kStatusNoResult), juce::dontSendNotification);
        break;
    case GenerationStatus::HubWindowClosed:
        statusLabel_.setText(tr(keys::kStatusHubClosed), juce::dontSendNotification);
        break;
    case GenerationStatus::UseHub:
        statusLabel_.setText(tr(keys::kStatusUseHub), juce::dontSendNotification);
        break;
    case GenerationStatus::Forwarded:
        statusLabel_.setText(tr(keys::kStatusForwarded), juce::dontSendNotification);
        break;
    case GenerationStatus::Idle:
        statusLabel_.setText({}, juce::dontSendNotification);
        break;
    }
    const bool failed = processor_.generationStatus() == GenerationStatus::AiFailed && !voiceLayout_;
    retryButton_.setVisible(failed);
    // A refinement has no offline form (SPEC 3.15): only "Retry" and the settings help there.
    offlineButton_.setVisible(failed && !processor_.lastJobIsRefine());
    autoOfflineButton_.setVisible(failed && !processor_.lastJobIsRefine());
    // The text of a refinement stays until it is applied, so a failure or a cancel keeps the wish for the next try.
    if (processor_.generationStatus() == GenerationStatus::Refined && refinePending_) {
        refineEditor_.clear();
    }
    if (processor_.generationStatus() != GenerationStatus::Generating) {
        refinePending_ = false;
    }
    refineButton_.setEnabled(processor_.generationStatus() != GenerationStatus::Generating);
    // A wrong key or model is solved in the settings (SPEC 7.9: "API key invalid -> settings").
    const auto& report = processor_.lastReport();
    errorSettingsButton_.setVisible(
        failed && report.outcome == mm::ai::AiOutcome::ProviderError &&
        (report.status == mm::ai::AiStatus::AuthFailed || report.status == mm::ai::AiStatus::ModelNotFound));
    autoOfflineButton_.setToggleState(processor_.autoOffline(), juce::dontSendNotification);
    const int statusButtons = (retryButton_.isVisible() ? 1 : 0) | (offlineButton_.isVisible() ? 2 : 0) |
                              (autoOfflineButton_.isVisible() ? 4 : 0) | (errorSettingsButton_.isVisible() ? 8 : 0);
    if (statusButtons != statusButtons_) { // the message gets the room the buttons leave
        statusButtons_ = statusButtons;
        resized();
    }
    generateButton_.setButtonText(
        tr(processor_.generatingWithAi() ? keys::kButtonCancelGeneration : keys::kButtonGenerate));
    if (groupText_.isNotEmpty()) {
        const auto generation = statusLabel_.getText();
        statusLabel_.setText(generation.isEmpty() ? groupText_ : groupText_ + "  |  " + generation,
                             juce::dontSendNotification);
    }
}

void PlaceholderEditor::showSlotMenu(int slot, juce::Point<int> where) {
    const size_t index = static_cast<size_t>(slot - 1);
    const bool filled = processor_.slotUsage()[index];
    juce::PopupMenu menu;
    menu.addItem(1, tr(keys::kSlotMenuCopy), filled);
    menu.addItem(2, tr(keys::kSlotMenuPaste), processor_.hasSlotClipboard());
    menu.addItem(3, tr(keys::kSlotMenuClear), filled);
    juce::PopupMenu swap;
    for (int other = 1; other <= static_cast<int>(mm::core::kSlotCount); ++other) {
        if (other != slot) {
            swap.addItem(100 + other, tr(keys::kSlotMenuSwapItem, {{"n", std::to_string(other)}}));
        }
    }
    menu.addSubMenu(tr(keys::kSlotMenuSwap), swap);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(juce::Rectangle<int>(where.x, where.y, 1, 1)),
                       [this, index](int choice) {
                           if (choice == 1) {
                               processor_.copySlot(index);
                           } else if (choice == 2) {
                               processor_.pasteSlot(index);
                           } else if (choice == 3) {
                               processor_.clearSlot(index);
                           } else if (choice > 100) {
                               processor_.swapSlots(index, static_cast<size_t>(choice - 101));
                           }
                           if (choice != 0) {
                               updateStatus();
                           }
                       });
}

void PlaceholderEditor::updatePlayhead() {
    const auto ppq = processor_.playheadPpq();
    if (voiceLayout_) {
        const auto ticks = static_cast<uint32_t>(rollView_.layout().bars) * mm::core::kTicksPerBar;
        const auto tick = ppq ? mm::core::playheadTick(*ppq, ticks) : std::nullopt;
        rollView_.setPlayheadTick(tick);
        if (tick && processor_.followPlayhead()) {
            mm::core::RollViewport view;
            view.startTick = rollView_.viewStart();
            view.spanTicks = rollView_.viewSpan();
            const auto next = mm::core::followViewport(view, *tick, rollView_.patternTicks());
            rollView_.setTimeView(next.startTick, next.spanTicks);
        }
        return;
    }
    const int playing = ppq ? processor_.playingSlotInfo().slot : 0;
    std::optional<double> followTick;
    for (auto& row : rows_) {
        auto& roll = row->roll();
        const bool here = ppq && row->slotIndex() + 1 == playing;
        const auto tick = here ? mm::core::playheadTick(*ppq, roll.patternTicks()) : std::nullopt;
        roll.setPlayheadTick(tick);
        if (tick && !followTick) {
            followTick = tick;
        }
    }
    if (followTick && processor_.followPlayhead() && !rows_.empty()) {
        // One window for all rolls: the first roll decides, the others take it over.
        const auto& first = rows_.front()->roll();
        const auto next = mm::core::followViewport(first.viewport(), *followTick, first.patternTicks());
        syncTimeView(next.startTick, next.spanTicks);
    }
}

void PlaceholderEditor::syncTimeView(uint32_t startTick, uint32_t spanTicks) {
    for (auto& row : rows_) {
        row->roll().setTimeView(startTick, spanTicks);
    }
}

void PlaceholderEditor::applyScale() {
    // The layout keeps its logical size; the window grows or shrinks with it, so the aspect ratio stays (SPEC 8.1).
    setScaleFactor(static_cast<float>(processor_.uiScalePct()) / 100.0f);
    syncZoomBox();
}

void PlaceholderEditor::syncZoomBox() {
    const int pct = processor_.uiScalePct();
    zoomBox_.setSelectedId(pct, juce::dontSendNotification); // an id of no entry clears the selection
    if (zoomBox_.getSelectedId() != pct) {
        zoomBox_.setText(tr(keys::kZoomItem, {{"value", std::to_string(pct)}}), juce::dontSendNotification);
    }
}

void PlaceholderEditor::applyLayout(bool voiceLayout) {
    voiceLayout_ = voiceLayout;
    palette_.close();
    styleBox_.setVisible(!voiceLayout);
    muteButton_.setVisible(voiceLayout);
    octaveBox_.setVisible(voiceLayout);
    openHubButton_.setVisible(voiceLayout);
    rollView_.setVisible(voiceLayout);
    dragHandle_->setVisible(voiceLayout);
    for (auto* component : std::initializer_list<juce::Component*>{&keyBox_, &scaleBox_, &barsBox_, &infoLabel_,
                                                                   &settingsButton_, &undoButton_, &redoButton_,
                                                                   &addVoiceButton_, &inspectorButton_, &commandsButton_}) {
        component->setVisible(!voiceLayout);
    }
    applyLevel();
    for (auto& button : slotButtons_) {
        button->setVisible(!voiceLayout);
    }
    for (auto& row : rows_) {
        row->setVisible(!voiceLayout);
    }
    // The voice UI is compact (SPEC 8.1); the hub UI has the size of the standard window (SPEC 8.1).
    setSize(voiceLayout ? 600 : 1000, voiceLayout ? 320 : 720);
    applyScale();
    resized();
}

void PlaceholderEditor::syncFields(const mm::core::InstanceSettings& settings) {
    const auto& generation = settings.generation;
    const auto profiles = processor_.styles().profiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
        if (profiles[i].id == generation.styleId && styleBox_.getSelectedId() != static_cast<int>(i) + 1) {
            styleBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
        }
    }
    const int keyId = generation.root ? static_cast<int>(*generation.root) + 2 : 1;
    if (keyBox_.getSelectedId() != keyId) {
        keyBox_.setSelectedId(keyId, juce::dontSendNotification);
    }
    int scaleId = 1;
    if (generation.scaleId) {
        const auto scales = mm::core::allScales();
        for (size_t i = 0; i < scales.size(); ++i) {
            if (scales[i].id == *generation.scaleId) {
                scaleId = static_cast<int>(i) + 2;
            }
        }
    }
    if (scaleBox_.getSelectedId() != scaleId) {
        scaleBox_.setSelectedId(scaleId, juce::dontSendNotification);
    }
    if (barsBox_.getSelectedId() != static_cast<int>(generation.lengthBars)) {
        barsBox_.setSelectedId(static_cast<int>(generation.lengthBars), juce::dontSendNotification);
    }
    if (!promptEditor_.hasKeyboardFocus(true)) {
        const auto text = juce::String::fromUTF8(generation.prompt.c_str());
        if (promptEditor_.getText() != text) {
            promptEditor_.setText(text, juce::dontSendNotification);
        }
    }
    if (!seedEditor_.hasKeyboardFocus(true)) {
        const juce::String text = generation.seed ? juce::String(std::to_string(*generation.seed)) : juce::String();
        if (seedEditor_.getText() != text) {
            seedEditor_.setText(text, juce::dontSendNotification);
        }
    }
    if (static_cast<int>(energySlider_.getValue()) != generation.energyPct) {
        energySlider_.setValue(generation.energyPct, juce::dontSendNotification);
    }
    if (static_cast<int>(creativitySlider_.getValue()) != generation.creativityPct) {
        creativitySlider_.setValue(generation.creativityPct, juce::dontSendNotification);
    }
    if (static_cast<int>(chromaticSlider_.getValue()) != generation.chromaticPct.value_or(-1)) {
        chromaticSlider_.setValue(generation.chromaticPct.value_or(-1), juce::dontSendNotification);
    }
    if (static_cast<int>(personalSlider_.getValue()) != generation.personalPct) {
        personalSlider_.setValue(generation.personalPct, juce::dontSendNotification);
    }
    const int hatId = generation.hatMode == mm::core::HatMode::With      ? 2
                      : generation.hatMode == mm::core::HatMode::Against ? 3
                                                                         : 1;
    if (hatModeBox_.getSelectedId() != hatId) {
        hatModeBox_.setSelectedId(hatId, juce::dontSendNotification);
    }
    if (startBox_.getSelectedId() != (settings.legato ? 2 : 1)) {
        startBox_.setSelectedId(settings.legato ? 2 : 1, juce::dontSendNotification);
    }
    if (quantBox_.getSelectedId() != static_cast<int>(settings.quantize) + 1) {
        quantBox_.setSelectedId(static_cast<int>(settings.quantize) + 1, juce::dontSendNotification);
    }
    const bool outputNone = settings.outputMode == mm::core::OutputMode::None;
    if (outputBox_.getSelectedId() != (outputNone ? 2 : 1)) {
        outputBox_.setSelectedId(outputNone ? 2 : 1, juce::dontSendNotification);
    }
    if (voiceBox_.getSelectedId() != settings.outputVoice) {
        voiceBox_.setSelectedId(settings.outputVoice, juce::dontSendNotification);
    }
}

bool PlaceholderEditor::keyPressed(const juce::KeyPress& press) {
    if (voiceLayout_ || !globalSettings().ready()) {
        return false;
    }
    const auto chord = chordOf(press);
    if (!chord.has_value()) {
        return false;
    }
    const std::string action = mm::core::actionForChord(globalSettings().get().shortcuts, *chord);
    if (action.empty()) {
        return false;
    }
    // without a modifier a key belongs to a text field that has the focus (the prompt, the refinement, the seed)
    if (mm::core::hasNoModifier(*chord) &&
        dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr) {
        return false;
    }
    runShortcut(action);
    return true; // the key was assigned to an action: the host does not get it
}

void PlaceholderEditor::openPalette() {
    if (voiceLayout_) {
        return;
    }
    const int width = std::min(420, getWidth() - 40);
    const int height = std::min(340, getHeight() - 80);
    palette_.setBounds((getWidth() - width) / 2, 56, width, height);
    palette_.open();
}

bool PlaceholderEditor::runShortcut(const std::string& action) {
    if (action == "palette") {
        if (voiceLayout_) {
            return false;
        }
        openPalette();
        return true;
    }
    const auto press = [](juce::Button& button) {
        if (!button.isEnabled()) {
            return false;
        }
        if (button.onClick) {
            button.onClick();
        }
        return true;
    };
    // the roll the keys of the notes work on: one with a selection, else the one of the focus, else the first
    const auto targetRoll = [this]() -> EditableRoll* {
        for (auto& row : rows_) {
            if (!row->roll().selection().empty()) {
                return &row->roll();
            }
        }
        if (focusedVoice_ >= 1 && static_cast<size_t>(focusedVoice_) <= rows_.size()) {
            return &rows_[static_cast<size_t>(focusedVoice_ - 1)]->roll();
        }
        return rows_.empty() ? nullptr : &rows_.front()->roll();
    };
    // these do not wait for the state of the buttons (the timer refreshes it): the processor knows what can be done
    const auto slotOfInfo = static_cast<size_t>(std::max(shownInfo_.slot, 1) - 1);
    if (action == "undo" || action == "redo") {
        const bool done = action == "undo" ? processor_.undo() : processor_.redo();
        updateStatus();
        updateActionButtons();
        return done;
    }
    if (action == "history_back" || action == "history_forward") {
        const bool done = action == "history_back" ? processor_.historyBack(slotOfInfo) : processor_.historyForward(slotOfInfo);
        updateStatus();
        updateActionButtons();
        return done;
    }
    if (action == "generate") {
        return press(generateButton_);
    }
    if (action == "refine_send") {
        return refineButton_.isVisible() && press(refineButton_);
    }
    if (action == "vary") {
        if (focusedVoice_ >= 1) {
            varyVoice(static_cast<size_t>(focusedVoice_ - 1), std::nullopt);
        } else {
            varyVoice(std::nullopt, std::nullopt);
        }
        return true;
    }
    if (action == "focus_all") {
        setFocusedVoice(0);
        return true;
    }
    if (action.rfind("focus_", 0) == 0 && action.size() == 7) {
        const int voice = action[6] - '0';
        if (voice >= 1 && static_cast<size_t>(voice) <= rows_.size()) {
            setFocusedVoice(voice);
            return true;
        }
        return false;
    }
    if (action == "grid_finer" || action == "grid_coarser") {
        // the divisions of the box are its item ids: 1/4, 1/8, 1/16, 1/32 and so on
        std::vector<int> ids;
        for (int i = 0; i < gridBox_.getNumItems(); ++i) {
            ids.push_back(gridBox_.getItemId(i));
        }
        std::sort(ids.begin(), ids.end());
        const auto at = std::find(ids.begin(), ids.end(), gridBox_.getSelectedId());
        if (at == ids.end()) {
            return false;
        }
        const auto next = action == "grid_finer" ? std::next(at) : (at == ids.begin() ? ids.end() : std::prev(at));
        if (next == ids.end()) {
            return false;
        }
        gridBox_.setSelectedId(*next, juce::sendNotificationSync);
        return true;
    }
    if (action == "triplets") {
        tripletButton_.setToggleState(!tripletButton_.getToggleState(), juce::dontSendNotification);
        if (tripletButton_.onClick) {
            tripletButton_.onClick(); // the same as a click
        }
        return true;
    }
    if (action == "snap_toggle") {
        snapBox_.setSelectedId(snapBox_.getSelectedId() == 2 ? 1 : 2, juce::sendNotificationSync);
        return true;
    }
    if (action == "fold_toggle") {
        globalSettings().update([](mm::core::GlobalSettings& settings) { settings.rollFold = !settings.rollFold; });
        applyRollHelpers();
        return true;
    }
    EditableRoll* roll = targetRoll();
    if (roll == nullptr) {
        return false;
    }
    const auto gridTicks = static_cast<int64_t>(roll->snap().grid.ticks());
    if (action == "duplicate") {
        roll->duplicateSelection();
    } else if (action == "select_all") {
        roll->selectAll();
    } else if (action == "delete") {
        roll->deleteSelection();
    } else if (action == "pitch_up") {
        roll->moveSelection(0, 1);
    } else if (action == "pitch_down") {
        roll->moveSelection(0, -1);
    } else if (action == "octave_up") {
        roll->moveSelection(0, 1, true);
    } else if (action == "octave_down") {
        roll->moveSelection(0, -1, true);
    } else if (action == "move_left") {
        roll->moveSelection(-gridTicks);
    } else if (action == "move_right") {
        roll->moveSelection(gridTicks);
    } else if (action == "longer") {
        roll->resizeSelection(static_cast<int32_t>(gridTicks));
    } else if (action == "shorter") {
        roll->resizeSelection(-static_cast<int32_t>(gridTicks));
    } else if (action == "accent") {
        roll->toggleAccent();
    } else if (action == "slide") {
        roll->toggleSlide();
    } else if (action == "ratchet_off") {
        roll->setRatchet(1);
    } else {
        return false;
    }
    return true;
}

void PlaceholderEditor::applyRollHelpers() {
    const auto settings = globalSettings().ready() ? globalSettings().get() : mm::core::GlobalSettings{};
    for (auto& row : rows_) {
        row->roll().setHelpers(settings.rollFold, settings.rollGuides, settings.rollMarkers);
    }
    resized(); // the marker row changes the height of the notes
}

void PlaceholderEditor::applyEditSettings() {
    const mm::core::EditGrid grid{static_cast<uint32_t>(std::max(gridBox_.getSelectedId(), 4)),
                                  tripletButton_.getToggleState()};
    const auto snap = snapBox_.getSelectedId() == 2 ? mm::core::PitchSnap::Chromatic : mm::core::PitchSnap::Scale;
    const int middleC = globalSettings().ready() ? globalSettings().get().middleC : 3;
    for (auto& row : rows_) {
        row->roll().setMiddleC(middleC);
        row->roll().setEditSettings(grid, snap);
    }
    applyRollHelpers();
}

void PlaceholderEditor::setFocusedVoice(int voice) {
    focusedVoice_ = voice;
    applyFocus();
}

void PlaceholderEditor::applyFocus() {
    if (focusedVoice_ < 0 || focusedVoice_ > static_cast<int>(rows_.size())) {
        focusedVoice_ = 0; // the focused voice is gone: back to all
    }
    for (size_t i = 0; i < rows_.size(); ++i) {
        const int voice = static_cast<int>(i) + 1;
        rows_[i]->setCollapsed(focusedVoice_ != 0 && focusedVoice_ != voice);
        rows_[i]->setFocused(focusedVoice_ == voice);
    }
    resized();
}

void PlaceholderEditor::updateVoiceNames() {
    const auto names = processor_.playingVoiceNames();
    bool renamed = false;
    for (int voice = 1; voice <= mm::core::kMaxVoices; ++voice) {
        const auto plain = tr(keys::kVoiceN, {{"n", std::to_string(voice)}});
        juce::String text = plain;
        const auto index = static_cast<size_t>(voice) - 1;
        if (index < names.size() && names[index].name != plain) {
            text = juce::String(voice) + " \xc2\xb7 " + names[index].name; // the middle dot
        }
        if (voiceBox_.getItemText(voice - 1) != text) {
            voiceBox_.changeItemText(voice, text);
            renamed = true;
        }
    }
    if (renamed) { // the box keeps the old text of the chosen item until it is chosen again
        voiceBox_.setSelectedId(processor_.instanceSettings().outputVoice, juce::dontSendNotification);
    }
}

void PlaceholderEditor::updateGroupPage() {
    using Info = mm::core::MemberInfo;
    const auto fill = [this](std::vector<GroupPanel::Row> rows, juce::String message, juce::StringArray warnings) {
        groupPanel_.setContent(std::move(rows), std::move(message), std::move(warnings), tr(keys::kGroupWarnOk));
    };
    if (processor_.instanceSettings().role != mm::core::InstanceRole::Hub) {
        fill({}, tr(keys::kGroupPageNotHub), {});
        return;
    }
    const auto names = processor_.playingVoiceNames();
    std::vector<mm::core::VoiceRole> roles;
    for (const auto& name : names) {
        roles.push_back(name.role);
    }
    if (names.empty()) { // an empty slot: the voices of the settings
        roles = shownRoles({});
    }
    if (roles.empty()) {
        fill({}, tr(keys::kGroupPageNoVoices), {});
        return;
    }
    // The voice instances in order of age: "Instance n" counts them like the registry does.
    std::vector<Info> voices;
    for (const auto& member : processor_.groupMembers()) {
        if (member.role == mm::core::InstanceRole::Voice) {
            voices.push_back(member);
        }
    }
    std::vector<GroupPanel::Row> rows;
    juce::StringArray warnings;
    for (size_t i = 0; i < roles.size(); ++i) {
        const int voice = static_cast<int>(i) + 1;
        GroupPanel::Row row;
        row.role = roles[i];
        row.name = i < names.size() ? names[i].name : tr(mm::core::text::roleTextKey(roles[i]));
        std::vector<size_t> playing;
        for (size_t v = 0; v < voices.size(); ++v) {
            if (voices[v].outputVoice == voice) {
                playing.push_back(v);
            }
        }
        if (playing.empty()) {
            row.assigned = tr(keys::kGroupRowNone);
            row.warn = true;
            warnings.add(tr(keys::kGroupWarnMissing, {{"voice", std::to_string(voice)}}));
        } else if (playing.size() == 1) {
            row.assigned = tr(keys::kGroupRowInstance, {{"n", std::to_string(playing.front() + 1)}});
            if (!voices[playing.front()].followsHub) {
                row.follow = tr(keys::kGroupRowOwnSlot);
            }
        } else {
            row.assigned = tr(keys::kGroupRowDuplicate, {{"n", std::to_string(playing.size())}});
            row.warn = true;
            warnings.add(tr(keys::kGroupWarnDuplicate, {{"voice", std::to_string(voice)}, {"n", std::to_string(playing.size())}}));
        }
        rows.push_back(std::move(row));
    }
    for (size_t v = 0; v < voices.size(); ++v) {
        if (voices[v].outputVoice > static_cast<int>(roles.size())) {
            warnings.add(tr(keys::kGroupWarnExtra, {{"n", std::to_string(v + 1)},
                                                    {"voice", std::to_string(voices[v].outputVoice)}}));
        }
    }
    if (processor_.otherFormatInProcess()) {
        warnings.add(tr(keys::kGroupMixedFormats).trim().trimCharactersAtStart("|").trim());
    }
    fill(std::move(rows), {}, std::move(warnings));
}

void PlaceholderEditor::setInspectorOpen(bool open) {
    inspectorOpen_ = open;
    inspectorButton_.setToggleState(open, juce::dontSendNotification);
    applyPanels();
    resized();
}

void PlaceholderEditor::setInspectorPage(InspectorPage page) {
    inspectorPage_ = page;
    for (size_t i = 0; i < inspectorTabs_.size(); ++i) {
        inspectorTabs_[i].setToggleState(static_cast<InspectorPage>(i) == page, juce::dontSendNotification);
    }
    applyPanels();
    resized();
}

void PlaceholderEditor::applyPanels() {
    using Page = InspectorPage;
    const bool hub = !voiceLayout_;
    const bool open = hub && inspectorOpen_;
    const bool role = processor_.instanceSettings().role == mm::core::InstanceRole::Hub;
    const auto page = [&](Page which, std::initializer_list<juce::Component*> basic,
                          std::initializer_list<juce::Component*> advanced) {
        const bool here = open && inspectorPage_ == which;
        for (auto* component : basic) {
            component->setVisible(here);
        }
        for (auto* component : advanced) {
            component->setVisible(here && expert_);
        }
    };
    for (auto& tab : inspectorTabs_) {
        tab.setVisible(open);
    }
    groupPanel_.setVisible(open && inspectorPage_ == Page::Group);
    page(Page::Instance, {&voiceBar_, &removeVoiceButton_, &referencesButton_, &libraryButton_, &expertButton_}, {});
    page(Page::Pattern, {&referenceSetBox_, &ratingBox_},
         {&seedEditor_, &randomButton_, &chromaticLabel_, &chromaticSlider_, &personalLabel_, &personalSlider_,
          &hatModeLabel_, &hatModeBox_, &startBox_, &quantBox_});
    page(Page::Edit, {&previewButton_, &drumsButton_, &historyBackButton_, &historyForwardButton_, &historyLabel_},
         {&gridBox_, &tripletButton_, &snapBox_, &strengthLabel_, &strengthSlider_, &varyAllButton_});
    page(Page::Evolve, {&evolveButton_, &evolveBackButton_, &evolveStageLabel_, &evolveKeepButton_},
         {&evolveEveryBox_, &evolveOriginalButton_, &evolveStrengthLabel_, &evolveStrengthSlider_});
    // Both windows show these; the hub keeps them in the inspector.
    const bool onInstance = open && inspectorPage_ == Page::Instance;
    roleBox_.setVisible(!hub || onInstance);
    voiceBox_.setVisible(!hub || onInstance);
    zoomBox_.setVisible(!hub || onInstance);
    helpButton_.setVisible(!hub || onInstance);
    outputBox_.setVisible(hub && onInstance && role);
    followButton_.setVisible(!hub || (open && inspectorPage_ == Page::Edit));
    // The dock writes a new pattern or changes the one of the slot (D-252).
    const bool changing = refining();
    modeButton_.setVisible(hub);
    modeButton_.setEnabled(shownInfo_.filled);
    modeButton_.setButtonText(tr(changing ? keys::kModeRefine : keys::kModeNew));
    for (auto* component : std::initializer_list<juce::Component*>{&promptEditor_, &energySlider_, &creativitySlider_,
                                                                   &energyLabel_, &creativityLabel_}) {
        component->setVisible(hub && !changing);
    }
    for (auto* component :
         std::initializer_list<juce::Component*>{&refineEditor_, &refineScopeBox_, &refineButton_}) {
        component->setVisible(hub && changing);
    }
    generateButton_.setVisible(!hub || !changing);
}

void PlaceholderEditor::applyLevel() {
    // The expert controls (seed, grid, snapping, strength of the variation, ...) are the advanced part of the pages of
    // the inspector (SPEC 8.1, D-252).
    expertButton_.setToggleState(expert_, juce::dontSendNotification);
    applyPanels();
    resized();
}

void PlaceholderEditor::applyStoredSettings() {
    auto& store = globalSettings();
    if (settingsApplied_ || !store.ready()) {
        return;
    }
    settingsApplied_ = true;
    const auto stored = store.get();
    expert_ = stored.expert;
    strengthSlider_.setValue(stored.variationStrength, juce::dontSendNotification);
    gridBox_.setSelectedId(static_cast<int>(stored.grid.division), juce::dontSendNotification);
    tripletButton_.setToggleState(stored.grid.triplet, juce::dontSendNotification);
    snapBox_.setSelectedId(stored.snapChromatic ? 2 : 1, juce::dontSendNotification);
    applyEditSettings();
    applyLevel();
}

void PlaceholderEditor::saveEditSettings() {
    globalSettings().update([this](mm::core::GlobalSettings& global) {
        global.grid = {static_cast<uint32_t>(std::max(gridBox_.getSelectedId(), 4)), tripletButton_.getToggleState()};
        global.snapChromatic = snapBox_.getSelectedId() == 2;
    });
}

void PlaceholderEditor::varyVoice(std::optional<size_t> voice, std::optional<size_t> slot) {
    processor_.vary(variationStrength(), voice, std::nullopt, slot);
    updateStatus();
}

void PlaceholderEditor::applyEvolveSettings() {
    processor_.setEvolveSettings({evolveEveryBox_.getSelectedId(), evolveOriginalButton_.getToggleState(),
                                  static_cast<int>(evolveStrengthSlider_.getValue())});
}

void PlaceholderEditor::updatePreviewButton() {
    const bool previewing = processor_.previewing();
    if (previewButton_.getToggleState() != previewing) { // the host may end the preview by starting
        previewButton_.setButtonText(tr(previewing ? keys::kButtonPreviewStop : keys::kButtonPreview));
        previewButton_.setToggleState(previewing, juce::dontSendNotification);
    }
}

void PlaceholderEditor::updateActionButtons() {
    updatePreviewButton();
    undoButton_.setEnabled(processor_.canUndo());
    redoButton_.setEnabled(processor_.canRedo());
    drumsButton_.setEnabled(shownInfo_.drumReference || processor_.drumReference().has_value());
    const auto& info = shownInfo_;
    historyBackButton_.setEnabled(info.historyIndex > 0 && info.historySize > 0);
    historyForwardButton_.setEnabled(info.historyIndex + 1 < info.historySize);
    const auto evolve = processor_.evolveInfo();
    if (evolveButton_.getToggleState() != evolve.on) { // e.g. an offline render switched it off
        evolveButton_.setToggleState(evolve.on, juce::dontSendNotification);
    }
    evolveBackButton_.setEnabled(evolve.on && evolve.canBack);
    evolveKeepButton_.setEnabled(evolve.on && evolve.changed);
    evolveStageLabel_.setText(evolve.on && evolve.layerActive
                                  ? tr(keys::kLabelEvolveStage, {{"n", std::to_string(evolve.stage)}})
                                  : tr(keys::kLabelEvolveStageOff),
                              juce::dontSendNotification);
    historyLabel_.setText(info.historySize == 0 ? tr(keys::kLabelHistoryNone)
                                                : tr(keys::kLabelHistory, {{"n", std::to_string(info.historyIndex + 1)},
                                                                           {"m", std::to_string(info.historySize)}}),
                          juce::dontSendNotification);
    bool anyFilled = false;
    for (auto& row : rows_) {
        row->updateButtons();
        anyFilled = anyFilled || row->slotIndex() >= 0;
    }
    varyAllButton_.setEnabled(anyFilled);
}

void PlaceholderEditor::correctSlotKey(const mm::core::InstanceSettings& settings) {
    // A slot with a locked voice keeps it: the key the musician picks then corrects the harmony found for it
    // (SPEC 3.18); without a locked voice the key only applies to the next generation.
    if (settings.generation.root && settings.generation.scaleId) {
        processor_.correctKey(*settings.generation.root, *settings.generation.scaleId);
    }
    updateStatus();
}

void PlaceholderEditor::rebuildRows(size_t count) {
    rows_.clear();
    for (size_t i = 0; i < count; ++i) {
        auto row = std::make_unique<VoiceRow>(processor_, static_cast<int>(i) + 1);
        row->onFocusClicked = [this](int voice) { setFocusedVoice(focusedVoice_ == voice ? 0 : voice); };
        row->onVaryClicked = [this, rowPtr = row.get()](int voice) {
            if (rowPtr->slotIndex() >= 0) {
                varyVoice(static_cast<size_t>(voice - 1), static_cast<size_t>(rowPtr->slotIndex()));
            }
        };
        row->onAction = [this] { updateStatus(); };
        row->roll().onTimeViewChanged = [this](uint32_t start, uint32_t span) { syncTimeView(start, span); };
        row->onMidiDropped = [this](int voice, const juce::File& file) {
            processor_.importVoice(file, static_cast<size_t>(voice - 1));
            updateStatus();
        };
        addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
    }
    applyEditSettings();
    // From three voices on only one roll is open and the others are strips: eight full rolls would not fit (D-249).
    if (count > kOpenRollsMax && focusedVoice_ == 0) {
        focusedVoice_ = 1;
    }
    applyFocus();
    shownVoices_.clear();
    resized();
}

bool PlaceholderEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    return voiceLayout_ && firstMidiFile(files) != juce::File();
}

void PlaceholderEditor::filesDropped(const juce::StringArray& files, int, int) {
    if (const auto file = firstMidiFile(files); voiceLayout_ && file != juce::File()) {
        processor_.importVoice(file);
        updateStatus();
    }
}

void PlaceholderEditor::startRefine() {
    const int id = refineScopeBox_.getSelectedId();
    std::optional<size_t> voice;
    std::optional<size_t> phrase;
    if (id >= 200) {
        phrase = static_cast<size_t>(id - 200);
    } else if (id >= 100) {
        voice = static_cast<size_t>(id - 100);
    }
    refinePending_ = processor_.refine(refineEditor_.getText().toStdString(), voice, phrase);
    updateStatus();
}

void PlaceholderEditor::updateRefineControls() {
    const auto targets = processor_.refineTargets();
    if (targets == shownTargets_) {
        return;
    }
    shownTargets_ = targets;
    const int before = refineScopeBox_.getSelectedId();
    refineScopeBox_.clear(juce::dontSendNotification);
    refineScopeBox_.addItem(tr(keys::kRefineScopeAll), 1);
    for (size_t i = 0; i < targets.voices.size(); ++i) {
        if (i >= 2) {
            break; // the AI writes bass and melody: the other voices are not part of a refinement (D-250)
        }
        refineScopeBox_.addItem(tr(mm::core::text::roleTextKey(targets.voices[i])), 100 + static_cast<int>(i));
    }
    if (targets.phrases.size() > 1) { // one phrase over the whole pattern is the whole pattern
        for (size_t i = 0; i < targets.phrases.size(); ++i) {
            const auto [start, length] = targets.phrases[i];
            refineScopeBox_.addItem(
                tr(keys::kRefineScopePhrase, {{"n", std::to_string(start + 1)}, {"m", std::to_string(start + length)}}),
                200 + static_cast<int>(i));
        }
    }
    const bool stillThere = before != 0 && refineScopeBox_.indexOfItemId(before) >= 0;
    refineScopeBox_.setSelectedId(stillThere ? before : 1, juce::dontSendNotification);
}

std::vector<mm::core::VoiceRole> PlaceholderEditor::shownRoles(const std::vector<ProcessorBase::VoiceView>& views) const {
    std::vector<mm::core::VoiceRole> roles;
    if (views.empty()) { // an empty slot: bass, melody and the voices the next pattern will get
        roles = {mm::core::VoiceRole::Bass, mm::core::VoiceRole::Melody};
        const auto& extra = processor_.instanceSettings().generation.extraVoices;
        roles.insert(roles.end(), extra.begin(), extra.end());
    } else {
        for (const auto& view : views) {
            roles.push_back(view.role);
        }
    }
    return roles;
}

void PlaceholderEditor::showAddVoiceMenu() {
    juce::PopupMenu menu;
    const auto roles = {mm::core::VoiceRole::Stabs, mm::core::VoiceRole::Arp, mm::core::VoiceRole::Counter};
    int id = 1;
    for (const auto role : roles) {
        menu.addItem(id++, tr(mm::core::text::roleTextKey(role)));
    }
    const std::vector<mm::core::VoiceRole> choice(roles);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addVoiceButton_),
                       [this, safe = juce::Component::SafePointer<PlaceholderEditor>(this), choice](int result) {
                           if (safe == nullptr || result < 1 || static_cast<size_t>(result) > choice.size()) {
                               return;
                           }
                           processor_.addVoice(choice[static_cast<size_t>(result) - 1]);
                           updateStatus();
                           updateFullView();
                       });
}

void PlaceholderEditor::updateFullView() {
    updateRefineControls();
    syncFields(processor_.instanceSettings());

    // Slot strip: a filled slot is lighter, the selected one has the accent colour.
    const auto used = processor_.slotUsage();
    const int selected = processor_.activeSlot();
    for (size_t i = 0; i < slotButtons_.size(); ++i) {
        auto& button = *slotButtons_[i];
        const bool isSelected = static_cast<int>(i) + 1 == selected;
        button.setToggleState(isSelected, juce::dontSendNotification);
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(used[i] ? 0xff3a3a44 : 0xff25252b));
        button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4fc3a1));
        button.setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(used[i] ? 0.95f : 0.45f));
        button.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    }

    // One row per voice of the playing slot (two, bass and melody, while the slot is empty).
    const auto views = processor_.playingVoices();
    const auto roles = shownRoles(views);
    const size_t count = roles.size();
    if (rows_.size() != count) {
        rebuildRows(count);
    }
    voiceBar_.setVoices(roles, focusedVoice_);
    addVoiceButton_.setEnabled(count < static_cast<size_t>(mm::core::kMaxVoices));
    removeVoiceButton_.setEnabled(count > 2);
    bool changed = shownVoices_.size() != views.size();
    for (size_t i = 0; !changed && i < views.size(); ++i) {
        changed = !views[i].sameSource(shownVoices_[i]);
    }
    if (changed) {
        shownVoices_ = views;
        for (size_t i = 0; i < rows_.size(); ++i) {
            rows_[i]->setView(i < views.size() ? &views[i] : nullptr,
                              tr(mm::core::text::roleTextKey(roles[i])), mm::core::isMelodicRole(roles[i]));
        }
        if (!rows_.empty() && rows_.front()->roll().patternTicks() > 0) { // the first roll holds the shared window
            const auto& window = rows_.front()->roll().viewport();
            syncTimeView(window.startTick, window.spanTicks);
        }
    }
    for (auto& row : rows_) {
        row->updateMuted();
    }

    const auto info = processor_.playingSlotInfo();
    const int transposed = processor_.effectiveTranspose();
    if (!(info == shownInfo_) || infoLabel_.getText().isEmpty() || transposed != shownTranspose_) {
        shownInfo_ = info;
        shownTranspose_ = transposed;
        if (!info.filled) {
            infoLabel_.setText(tr(keys::kSlotEmpty, {{"slot", std::to_string(info.slot)}}), juce::dontSendNotification);
        } else {
            const auto* style = processor_.styles().find(info.styleId);
            infoLabel_.setText(tr(keys::kSlotInfo, {{"slot", std::to_string(info.slot)},
                                                    {"style", style != nullptr ? localized(style->nameDe, style->nameEn) : info.styleId},
                                                    {"key", kKeyNames[info.root % 12]},
                                                    {"scale", scaleLabel(info.scaleId).toStdString()},
                                                    {"length", barsText(info.lengthBars).toStdString()},
                                                    {"seed", std::to_string(info.seed)},
                                                    {"winner", std::to_string(info.winnerSeed)}}),
                               juce::dontSendNotification);
        }
        if (transposed != 0) { // the live transposition (SPEC 3.16) is shown next to the slot
            infoLabel_.setText(infoLabel_.getText() + "  |  " +
                                   tr(keys::kSlotTransposed, {{"n", (transposed > 0 ? "+" : "") + std::to_string(transposed)}}),
                               juce::dontSendNotification);
        }
    }
    {
        const int rating = info.filled ? processor_.slotRating(static_cast<size_t>(info.slot - 1)) : 0;
        ratingBox_.setEnabled(info.filled);
        if (ratingBox_.getSelectedId() != rating + 1) {
            ratingUpdating_ = true;
            ratingBox_.setSelectedId(rating + 1, juce::dontSendNotification);
            ratingUpdating_ = false;
        }
    }
    updateActionButtons();
}

void PlaceholderEditor::updateVoiceView() {
    // The mute button follows the host parameter of the voice this instance plays, also under automation.
    const int voice = processor_.instanceSettings().outputVoice;
    if (voice != muteVoice_) {
        muteVoice_ = voice;
        muteAttachment_.reset();
        if (auto* parameter = processor_.parameters().getParameter(mm::core::muteParameterId(voice))) {
            muteAttachment_ = std::make_unique<juce::ButtonParameterAttachment>(*parameter, muteButton_);
        }
    }
    const bool mutedByHub = processor_.mutedByHub();
    muteButton_.setButtonText(tr(mutedByHub ? keys::kButtonMuteByHub : keys::kButtonMute));
    rollView_.setDimmed(processor_.mutedByOwnSwitch() || mutedByHub);
    const int octaveId = processor_.instanceSettings().octave + 3;
    if (octaveBox_.getSelectedId() != octaveId) {
        octaveBox_.setSelectedId(octaveId, juce::dontSendNotification);
    }
    auto view = processor_.playingVoice();
    if (!view.sameSource(shown_) || shown_.origin == 0 /* nothing shown yet */) {
        shown_ = view;
        rollView_.setAccent(view.melody ? juce::Colour(0xffe0a458) : juce::Colour(0xff4fc3a1));
        rollView_.setContent(
            mm::core::layoutRoll(view.notes, view.lengthTicks),
            tr(keys::kRollTitle, {{"slot", std::to_string(view.slot)}, {"voice", view.displayName.toStdString()}}),
            !view.hasPattern);
    }
}

std::string PlaceholderEditor::referenceStyleId() const {
    const auto id = processor_.instanceSettings().generation.styleId;
    const auto* style = processor_.styles().findOrFallback(id);
    return style != nullptr ? style->id : id;
}

void PlaceholderEditor::refreshReferenceSets() {
    auto& store = referenceStore();
    const auto styleId = referenceStyleId();
    const auto key = styleId + "#" + std::to_string(store.revision());
    if (key == referenceSetKey_) {
        return;
    }
    referenceSetKey_ = key;
    const auto collection = store.collection(styleId);
    referenceSetIds_.clear();
    referenceSetBox_.clear(juce::dontSendNotification);
    int selected = 0;
    for (const auto& set : collection.sets) {
        referenceSetIds_.push_back(set.id);
        const int id = static_cast<int>(referenceSetIds_.size());
        referenceSetBox_.addItem(set.id == mm::core::kFavoritesSetId ? tr(keys::kReferencesSetFavorites)
                                                                     : juce::String::fromUTF8(set.name.c_str()),
                                 id);
        if (set.id == collection.activeSetId) {
            selected = id;
        }
    }
    referenceSetBox_.setSelectedId(selected, juce::dontSendNotification);
}

void PlaceholderEditor::timerCallback() {
    refreshReferenceSets();
    if (auto& store = globalSettings(); store.ready() && store.revision() != namingRevision_) {
        namingRevision_ = store.revision();
        const int middleC = store.get().middleC;
        for (auto& row : rows_) {
            row->roll().setMiddleC(middleC);
        }
        applyRollHelpers();
    }
    updateStatus();
    processor_.pollPreview();
    updatePreviewButton();
    processor_.updateExport();
}

void PlaceholderEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(MidiMaidLookAndFeel::kBackground));
    if (voiceLayout_) {
        g.setColour(juce::Colour(MidiMaidLookAndFeel::kSurface));
        g.fillRoundedRectangle(voiceStrip_.toFloat(), 6.0f);
    }
}

void PlaceholderEditor::layoutVoiceWindow() {
    // The voice window in the style of the hub (D-253): a header with the role and the voice, the lane with a strip of
    // its own on the left and the roll, the status line and a dock at the bottom with the one main action.
    constexpr int kPad = 12;
    constexpr int kGap = 8;
    constexpr int kControl = 34; // one height for every control of the window
    auto area = getLocalBounds();

    auto header = area.removeFromTop(44).reduced(kPad, 5);
    takeOverButton_.setBounds(header.removeFromRight(130));
    roleBox_.setBounds(header.removeFromLeft(110));
    header.removeFromLeft(kGap);
    voiceBox_.setBounds(header.removeFromLeft(110));
    header.removeFromLeft(kGap);
    followBox_.setBounds(header.removeFromLeft(170));
    outputBox_.setBounds(followBox_.getBounds()); // never visible together

    auto dock = area.removeFromBottom(48).reduced(kPad, 7);
    dock.removeFromRight(8); // the grip of the size corner stays free
    generateButton_.setBounds(dock.removeFromRight(130));
    dock.removeFromRight(kGap);
    zoomBox_.setBounds(dock.removeFromRight(110));
    dock.removeFromRight(kGap);
    helpButton_.setBounds(dock.removeFromRight(80));
    dock.removeFromRight(kGap);
    followButton_.setBounds(dock.removeFromRight(90));

    statusLabel_.setBounds(area.removeFromBottom(26).reduced(kPad, 0));

    auto lane = area.reduced(kPad, 2);
    voiceStrip_ = lane.removeFromLeft(150);
    auto strip = voiceStrip_.reduced(8, 8);
    muteButton_.setBounds(strip.removeFromTop(kControl));
    strip.removeFromTop(kGap);
    octaveBox_.setBounds(strip.removeFromTop(kControl));
    strip.removeFromTop(kGap);
    openHubButton_.setBounds(strip.removeFromTop(kControl));
    dragHandle_->setBounds(strip.removeFromBottom(40));
    lane.removeFromLeft(kGap);
    rollView_.setBounds(lane);
}

void PlaceholderEditor::resized() {
    scaleCorner_.setBounds(getWidth() - 16, getHeight() - 16, 16, 16);
    scaleCorner_.toFront(false);
    if (voiceLayout_) {
        layoutVoiceWindow();
        return;
    }
    // The hub window (D-252): a header with the musical basics and the slots, the lanes with all the height that is
    // left, the dock at the bottom and, only when asked for, the inspector on the right with everything else.
    auto area = getLocalBounds();
    constexpr int kPad = 12;
    takeOverButton_.setBounds({});
    followBox_.setBounds({});

    auto header = area.removeFromTop(40).reduced(kPad, 6);
    settingsButton_.setBounds(header.removeFromRight(90));
    header.removeFromRight(6);
    inspectorButton_.setBounds(header.removeFromRight(90));
    header.removeFromRight(6);
    commandsButton_.setBounds(header.removeFromRight(90));
    header.removeFromRight(6);
    redoButton_.setBounds(header.removeFromRight(90));
    header.removeFromRight(6);
    undoButton_.setBounds(header.removeFromRight(90));
    styleBox_.setBounds(header.removeFromLeft(170));
    header.removeFromLeft(6);
    keyBox_.setBounds(header.removeFromLeft(70));
    header.removeFromLeft(6);
    scaleBox_.setBounds(header.removeFromLeft(120));
    header.removeFromLeft(6);
    barsBox_.setBounds(header.removeFromLeft(80));

    auto strip = area.removeFromTop(30).reduced(kPad, 2);
    const int buttonWidth = strip.getWidth() / static_cast<int>(slotButtons_.size());
    for (auto& button : slotButtons_) {
        button->setBounds(strip.removeFromLeft(buttonWidth).reduced(1, 0));
    }

    if (inspectorOpen_) {
        auto side = area.removeFromRight(kInspectorWidth).reduced(kPad, 4);
        constexpr int kRow = 28;
        const auto row = [&side](int height) { return side.removeFromTop(height).reduced(0, 2); };
        auto tabs = row(kRow);
        const int tabWidth = tabs.getWidth() / static_cast<int>(inspectorTabs_.size());
        for (auto& tab : inspectorTabs_) {
            tab.setBounds(tabs.removeFromLeft(tabWidth).reduced(1, 0));
        }
        side.removeFromTop(6);
        const auto page = side; // every page starts at the same place; only one is visible
        const auto next = [&](juce::Rectangle<int>& rest) { return rest.removeFromTop(kRow).reduced(0, 2); };
        const auto pair = [&](juce::Rectangle<int> line, juce::Component& label, juce::Component& control, int labelWidth) {
            label.setBounds(line.removeFromLeft(labelWidth));
            control.setBounds(line);
        };
        {   // instance
            auto rest = page;
            roleBox_.setBounds(next(rest));
            voiceBox_.setBounds(next(rest));
            outputBox_.setBounds(next(rest));
            zoomBox_.setBounds(next(rest));
            referencesButton_.setBounds(next(rest));
            libraryButton_.setBounds(next(rest));
            helpButton_.setBounds(next(rest));
            expertButton_.setBounds(next(rest));
            voiceBar_.setBounds(rest.removeFromTop(32).reduced(0, 2).removeFromLeft(8 * 30));
            removeVoiceButton_.setBounds(next(rest));
        }
        {   // pattern
            auto rest = page;
            referenceSetBox_.setBounds(next(rest));
            ratingBox_.setBounds(next(rest).removeFromLeft(130));
            auto seedRow = next(rest);
            randomButton_.setBounds(seedRow.removeFromRight(120));
            seedRow.removeFromRight(8);
            seedEditor_.setBounds(seedRow);
            pair(next(rest), chromaticLabel_, chromaticSlider_, 80);
            pair(next(rest), personalLabel_, personalSlider_, 80);
            pair(next(rest), hatModeLabel_, hatModeBox_, 110);
            startBox_.setBounds(next(rest));
            quantBox_.setBounds(next(rest));
        }
        {   // edit
            auto rest = page;
            previewButton_.setBounds(next(rest));
            followButton_.setBounds(next(rest));
            drumsButton_.setBounds(next(rest));
            auto history = next(rest);
            historyBackButton_.setBounds(history.removeFromLeft(40));
            historyForwardButton_.setBounds(history.removeFromRight(40));
            historyLabel_.setBounds(history);
            auto grid = next(rest);
            gridBox_.setBounds(grid.removeFromLeft(80));
            grid.removeFromLeft(8);
            tripletButton_.setBounds(grid);
            snapBox_.setBounds(next(rest));
            pair(next(rest), strengthLabel_, strengthSlider_, 70);
            varyAllButton_.setBounds(next(rest));
        }
        {   // evolve
            auto rest = page;
            auto top = next(rest);
            evolveButton_.setBounds(top.removeFromLeft(100));
            top.removeFromLeft(6);
            evolveBackButton_.setBounds(top.removeFromLeft(40));
            evolveStageLabel_.setBounds(next(rest));
            evolveKeepButton_.setBounds(next(rest));
            evolveEveryBox_.setBounds(next(rest));
            evolveOriginalButton_.setBounds(next(rest));
            pair(next(rest), evolveStrengthLabel_, evolveStrengthSlider_, 110);
        }
        groupPanel_.setBounds(page);
    }

    // The status line: the message, the info of the slot and, after an error, the buttons that belong to it.
    auto statusRow = area.removeFromBottom(26).reduced(kPad, 0);
    statusRow.removeFromRight(16); // the grip of the size corner
    const auto take = [&](juce::Component& component, int width, int inset) {
        if (component.isVisible()) {
            component.setBounds(statusRow.removeFromRight(width).reduced(0, inset));
            statusRow.removeFromRight(8);
        }
    };
    take(autoOfflineButton_, 210, 0);
    take(offlineButton_, 140, 3);
    take(retryButton_, 140, 3);
    take(errorSettingsButton_, 120, 3);
    infoLabel_.setBounds(statusRow.removeFromRight(std::min(statusRow.getWidth() / 2, 440)));
    statusLabel_.setBounds(statusRow);

    // The dock: what to write (or change) and the one action that follows.
    auto dock = area.removeFromBottom(60).reduced(kPad, 6);
    modeButton_.setBounds(dock.removeFromLeft(70));
    dock.removeFromLeft(8);
    generateButton_.setBounds(dock.removeFromRight(110));
    refineButton_.setBounds(generateButton_.getBounds());
    dock.removeFromRight(8);
    auto feel = dock.removeFromRight(refining() ? 150 : 400);
    if (refining()) {
        refineScopeBox_.setBounds(feel.reduced(0, 10));
    } else {
        auto energy = feel.removeFromLeft(feel.getWidth() / 2);
        energyLabel_.setBounds(energy.removeFromLeft(64));
        energySlider_.setBounds(energy.reduced(0, 10));
        creativityLabel_.setBounds(feel.removeFromLeft(80));
        creativitySlider_.setBounds(feel.reduced(0, 10));
    }
    dock.removeFromRight(8);
    promptEditor_.setBounds(dock);
    refineEditor_.setBounds(dock.reduced(0, 10));

    // A ghost lane to add a voice, under the lanes.
    auto ghost = area.removeFromBottom(34).reduced(kPad, 2);
    addVoiceButton_.setBounds(ghost);
    area.reduce(0, 2);
    if (!rows_.empty()) {
        constexpr int kStripHeight = 36; // a collapsed row (D-159)
        const int collapsedCount = static_cast<int>(
            std::count_if(rows_.begin(), rows_.end(), [](const auto& row) { return row->collapsed(); }));
        const int open = static_cast<int>(rows_.size()) - collapsedCount;
        const int rowHeight = open > 0 ? (area.getHeight() - collapsedCount * kStripHeight) / open : 0;
        for (auto& row : rows_) {
            row->setBounds(area.removeFromTop(row->collapsed() ? kStripHeight : rowHeight));
        }
    }
}

} // namespace mm::plugin
