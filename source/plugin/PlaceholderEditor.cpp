#include "plugin/PlaceholderEditor.h"

#include "core/ParameterRegister.h"
#include "core/TextKeys.h"
#include "plugin/AiText.h"
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
        styleBox_.addItem(juce::String::fromUTF8(profiles[i].nameDe.c_str()), static_cast<int>(i) + 1);
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
                               style != nullptr ? juce::String::fromUTF8(style->nameDe.c_str()) : juce::String(id));
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
    outputBox_.setVisible(role == mm::core::InstanceRole::Hub);
    if (isVoice && processor_.lateSwitches() > 0) {
        group += tr(keys::kStatusLate, {{"n", std::to_string(processor_.lateSwitches())}});
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
        statusLabel_.setText(report.usedAi && report.belowMinScore
                                 ? tr(keys::kStatusBelowQuality, {{"score", std::to_string(report.score)}})
                                 : tr(done),
                             juce::dontSendNotification);
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
    styleBox_.setVisible(!voiceLayout);
    muteButton_.setVisible(voiceLayout);
    octaveBox_.setVisible(voiceLayout);
    openHubButton_.setVisible(voiceLayout);
    rollView_.setVisible(voiceLayout);
    dragHandle_->setVisible(voiceLayout);
    for (auto* component : std::initializer_list<juce::Component*>{
             &keyBox_, &scaleBox_, &barsBox_, &energySlider_, &creativitySlider_, &energyLabel_, &creativityLabel_,
             &infoLabel_, &promptEditor_, &refineEditor_, &refineScopeBox_, &refineButton_, &settingsButton_, &referencesButton_, &libraryButton_,
             &expertButton_, &undoButton_, &redoButton_, &drumsButton_, &historyBackButton_, &historyForwardButton_, &historyLabel_, &evolveButton_, &evolveBackButton_,
             &evolveStageLabel_, &evolveKeepButton_}) {
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
    const bool outputNone = settings.outputMode == mm::core::OutputMode::None;
    if (outputBox_.getSelectedId() != (outputNone ? 2 : 1)) {
        outputBox_.setSelectedId(outputNone ? 2 : 1, juce::dontSendNotification);
    }
    if (voiceBox_.getSelectedId() != settings.outputVoice) {
        voiceBox_.setSelectedId(settings.outputVoice, juce::dontSendNotification);
    }
}

void PlaceholderEditor::applyEditSettings() {
    const mm::core::EditGrid grid{static_cast<uint32_t>(std::max(gridBox_.getSelectedId(), 4)),
                                  tripletButton_.getToggleState()};
    const auto snap = snapBox_.getSelectedId() == 2 ? mm::core::PitchSnap::Chromatic : mm::core::PitchSnap::Scale;
    for (auto& row : rows_) {
        row->roll().setEditSettings(grid, snap);
    }
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

void PlaceholderEditor::applyLevel() {
    // The expert controls: seed, edit grid, snapping, strength of the variation (SPEC 8.1).
    const bool show = expert_ && !voiceLayout_;
    for (auto* component :
         std::initializer_list<juce::Component*>{&seedEditor_, &randomButton_, &gridBox_, &tripletButton_, &snapBox_,
                                                 &varyAllButton_, &strengthLabel_, &strengthSlider_, &evolveEveryBox_,
                                                 &evolveOriginalButton_, &evolveStrengthLabel_,
                                                 &evolveStrengthSlider_}) {
        component->setVisible(show);
    }
    expertButton_.setToggleState(expert_, juce::dontSendNotification);
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

void PlaceholderEditor::updateActionButtons() {
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
        refineScopeBox_.addItem(
            tr(targets.voices[i] == mm::core::VoiceRole::Bass ? keys::kVoiceBass : keys::kVoiceMelody),
            100 + static_cast<int>(i));
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
    const size_t count = views.empty() ? size_t{2} : views.size();
    if (rows_.size() != count) {
        rebuildRows(count);
    }
    bool changed = shownVoices_.size() != views.size();
    for (size_t i = 0; !changed && i < views.size(); ++i) {
        changed = !views[i].sameSource(shownVoices_[i]);
    }
    if (changed) {
        shownVoices_ = views;
        for (size_t i = 0; i < rows_.size(); ++i) {
            rows_[i]->setView(i < views.size() ? &views[i] : nullptr,
                              i == 0 ? tr(keys::kVoiceBass) : tr(keys::kVoiceMelody), i != 0);
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
                                                    {"style", style != nullptr ? style->nameDe : info.styleId},
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

void PlaceholderEditor::timerCallback() {
    updateStatus();
    processor_.updateExport();
}

void PlaceholderEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(MidiMaidLookAndFeel::kBackground));
}

void PlaceholderEditor::layoutVoiceWindow() {
    // One grid for the compact window (SPEC 8.1): 16 px at the sides, 10 px between controls, every control 34 px high
    // in rows of 52 px; the roll takes what is left.
    constexpr int kPad = 16;
    constexpr int kGap = 10;
    constexpr int kControl = 34;
    constexpr int kRow = 52;
    constexpr int kInset = (kRow - kControl) / 2;
    auto area = getLocalBounds();

    // Three columns that both rows share (110, 110 and 170 px), and the main action of each row at the right edge.
    constexpr int kCol1 = 110;
    constexpr int kCol2 = 110;
    constexpr int kCol3 = 170;
    constexpr int kAction = 130;

    auto footer = area.removeFromBottom(kRow).reduced(kPad, kInset);
    zoomBox_.setBounds(footer.removeFromRight(120));
    footer.removeFromRight(kGap);
    helpButton_.setBounds(footer.removeFromRight(90));
    footer.removeFromRight(kGap);
    followButton_.setBounds(footer.removeFromRight(90));
    dragHandle_->setBounds(footer.removeFromLeft(kCol1 + kGap + kCol2));

    auto roles = area.removeFromTop(kRow).reduced(kPad, kInset);
    takeOverButton_.setBounds(roles.removeFromRight(kAction));
    roleBox_.setBounds(roles.removeFromLeft(kCol1));
    roles.removeFromLeft(kGap);
    voiceBox_.setBounds(roles.removeFromLeft(kCol2));
    roles.removeFromLeft(kGap);
    followBox_.setBounds(roles.removeFromLeft(kCol3));
    outputBox_.setBounds(followBox_.getBounds()); // never visible together

    auto actions = area.removeFromTop(kRow).reduced(kPad, kInset);
    generateButton_.setBounds(actions.removeFromRight(kAction));
    muteButton_.setBounds(actions.removeFromLeft(kCol1));
    actions.removeFromLeft(kGap);
    octaveBox_.setBounds(actions.removeFromLeft(kCol2));
    actions.removeFromLeft(kGap);
    openHubButton_.setBounds(actions.removeFromLeft(kCol2));

    statusLabel_.setBounds(area.removeFromBottom(30).reduced(kPad, 0));
    rollView_.setBounds(area.reduced(kPad, 2));
}

void PlaceholderEditor::resized() {
    scaleCorner_.setBounds(getWidth() - 16, getHeight() - 16, 16, 16);
    scaleCorner_.toFront(false);
    if (voiceLayout_) {
        layoutVoiceWindow();
        return;
    }
    auto area = getLocalBounds();
    auto roles = area.removeFromTop(48).reduced(16, 10);
    zoomBox_.setBounds(roles.removeFromRight(110));
    roles.removeFromRight(8);
    helpButton_.setBounds(roles.removeFromRight(80));
    roles.removeFromRight(8);
    settingsButton_.setBounds(roles.removeFromRight(120));
    roles.removeFromRight(8);
    referencesButton_.setBounds(roles.removeFromRight(110));
    roles.removeFromRight(8);
    libraryButton_.setBounds(roles.removeFromRight(110));
    roles.removeFromRight(8);
    takeOverButton_.setBounds({}); // only a voice offers to take over: no room reserved here, the role box needs it
    followBox_.setBounds(roles.removeFromRight(150));
    outputBox_.setBounds(followBox_.getBounds()); // never visible together
    roles.removeFromRight(8);
    voiceBox_.setBounds(roles.removeFromRight(120));
    roles.removeFromRight(8);
    roleBox_.setBounds(roles.removeFromLeft(150));

    auto top = area.removeFromTop(48).reduced(16, 10);
    generateButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(8);

    // Hub UI: the settings of "Generate" in one row, then energy and creativity, the slot strip, the voices.
    if (expert_) {
        randomButton_.setBounds(top.removeFromRight(110));
        top.removeFromRight(8);
        seedEditor_.setBounds(top.removeFromRight(150));
        top.removeFromRight(8);
    }
    barsBox_.setBounds(top.removeFromRight(90));
    top.removeFromRight(8);
    scaleBox_.setBounds(top.removeFromRight(190));
    top.removeFromRight(8);
    keyBox_.setBounds(top.removeFromRight(110));
    top.removeFromRight(8);
    styleBox_.setBounds(top);

    promptEditor_.setBounds(area.removeFromTop(40).reduced(16, 6));
    auto refineRow = area.removeFromTop(40).reduced(16, 6);
    refineButton_.setBounds(refineRow.removeFromRight(120));
    refineRow.removeFromRight(8);
    refineScopeBox_.setBounds(refineRow.removeFromRight(180));
    refineRow.removeFromRight(8);
    refineEditor_.setBounds(refineRow);

    auto sliders = area.removeFromTop(40).reduced(16, 6);
    const int half = sliders.getWidth() / 2;
    auto left = sliders.removeFromLeft(half - 8);
    energyLabel_.setBounds(left.removeFromLeft(80));
    energySlider_.setBounds(left);
    sliders.removeFromLeft(16);
    creativityLabel_.setBounds(sliders.removeFromLeft(80));
    creativitySlider_.setBounds(sliders);

    auto strip = area.removeFromTop(40).reduced(16, 4);
    const int buttonWidth = strip.getWidth() / static_cast<int>(slotButtons_.size());
    for (auto& button : slotButtons_) {
        button->setBounds(strip.removeFromLeft(buttonWidth).reduced(2, 0));
    }

    auto statusRow = area.removeFromBottom(40).reduced(16, 0);
    // Only the buttons that are shown take room: without an error the message has the whole width.
    const auto take = [&](juce::Component& component, int width, int inset) {
        if (component.isVisible()) {
            component.setBounds(statusRow.removeFromRight(width).reduced(0, inset));
            statusRow.removeFromRight(8);
        }
    };
    take(autoOfflineButton_, 210, 0);
    take(offlineButton_, 140, 6);
    take(retryButton_, 140, 6);
    take(errorSettingsButton_, 120, 6);
    statusLabel_.setBounds(statusRow);
    infoLabel_.setBounds(area.removeFromBottom(28).reduced(16, 0));
    auto historyBar = area.removeFromBottom(32).reduced(16, 2);
    undoButton_.setBounds(historyBar.removeFromLeft(110));
    historyBar.removeFromLeft(8);
    redoButton_.setBounds(historyBar.removeFromLeft(110));
    historyBar.removeFromLeft(8);
    drumsButton_.setBounds(historyBar.removeFromLeft(110));
    historyBar.removeFromLeft(24);
    historyBackButton_.setBounds(historyBar.removeFromLeft(40));
    historyLabel_.setBounds(historyBar.removeFromLeft(120));
    historyForwardButton_.setBounds(historyBar.removeFromLeft(40));
    followButton_.setBounds(historyBar.removeFromRight(100));
    historyBar.removeFromLeft(24);
    evolveButton_.setBounds(historyBar.removeFromLeft(76));
    historyBar.removeFromLeft(6);
    evolveBackButton_.setBounds(historyBar.removeFromLeft(34));
    evolveStageLabel_.setBounds(historyBar.removeFromLeft(70));
    evolveKeepButton_.setBounds(historyBar.removeFromLeft(86));
    auto editBar = area.removeFromTop(32).reduced(16, 2);
    expertButton_.setBounds(editBar.removeFromLeft(120));
    editBar.removeFromLeft(8);
    varyAllButton_.setBounds(editBar.removeFromRight(140));
    editBar.removeFromRight(8);
    strengthSlider_.setBounds(editBar.removeFromRight(220));
    strengthLabel_.setBounds(editBar.removeFromRight(60));
    gridBox_.setBounds(editBar.removeFromLeft(80));
    editBar.removeFromLeft(8);
    tripletButton_.setBounds(editBar.removeFromLeft(90));
    editBar.removeFromLeft(8);
    snapBox_.setBounds(editBar.removeFromLeft(190));
    if (expert_) { // the settings of Evolve (SPEC 6.7) in a row of their own
        auto evolveRow = area.removeFromTop(32).reduced(16, 2);
        evolveEveryBox_.setBounds(evolveRow.removeFromLeft(190));
        evolveRow.removeFromLeft(8);
        evolveOriginalButton_.setBounds(evolveRow.removeFromLeft(150));
        evolveRow.removeFromLeft(16);
        evolveStrengthLabel_.setBounds(evolveRow.removeFromLeft(110));
        evolveStrengthSlider_.setBounds(evolveRow.removeFromLeft(240));
    }
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
