#include "plugin/PlaceholderEditor.h"

#include "core/ParameterRegister.h"
#include "plugin/GroupText.h"

namespace mm::plugin {

namespace {

constexpr const char* kKeyNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/// "natural_minor" -> "Natural minor"
juce::String scaleLabel(std::string_view id) {
    auto text = juce::String(id.data(), id.size()).replace("_", " ");
    return text.substring(0, 1).toUpperCase() + text.substring(1);
}

} // namespace

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

    outputBox_.addItem("Output: a voice", 1);
    outputBox_.addItem("Output: none", 2);
    outputBox_.setSelectedId(settings.outputMode == mm::core::OutputMode::None ? 2 : 1, juce::dontSendNotification);
    outputBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.outputMode = outputBox_.getSelectedId() == 2 ? mm::core::OutputMode::None : mm::core::OutputMode::OneVoice;
        processor_.setInstanceSettings(next);
    };
    addChildComponent(outputBox_);

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

    styleBox_.setComponentID("style");

    keyBox_.setComponentID("key");
    keyBox_.addItem("Key: auto", 1);
    for (int i = 0; i < 12; ++i) {
        keyBox_.addItem(juce::String("Key: ") + kKeyNames[i], i + 2);
    }
    keyBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        const int id = keyBox_.getSelectedId();
        if (id <= 1) {
            next.generation.root.reset();
        } else {
            next.generation.root = static_cast<mm::core::PitchClass>(id - 2);
        }
        processor_.setInstanceSettings(next);
    };
    addChildComponent(keyBox_);

    scaleBox_.setComponentID("scale");
    scaleBox_.addItem("Scale: auto", 1);
    {
        int id = 2;
        for (const auto& scale : mm::core::allScales()) {
            scaleBox_.addItem("Scale: " + scaleLabel(scale.id), id++);
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
        }
        processor_.setInstanceSettings(next);
    };
    addChildComponent(scaleBox_);

    barsBox_.setComponentID("bars");
    for (const int bars : {1, 2, 4, 8, 16}) {
        barsBox_.addItem(juce::String(bars) + (bars == 1 ? " bar" : " bars"), bars);
    }
    barsBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.generation.lengthBars = static_cast<uint32_t>(barsBox_.getSelectedId());
        processor_.setInstanceSettings(next);
    };
    addChildComponent(barsBox_);

    seedEditor_.setComponentID("seed");
    seedEditor_.setInputRestrictions(20, "0123456789");
    seedEditor_.setTextToShowWhenEmpty("Seed: random", juce::Colours::white.withAlpha(0.45f));
    const auto applySeed = [this] {
        auto next = processor_.instanceSettings();
        next.generation.seed = mm::core::parseSeed(seedEditor_.getText().toStdString());
        processor_.setInstanceSettings(next);
        syncFields(processor_.instanceSettings()); // a seed that does not fit goes back to "random"
    };
    seedEditor_.onReturnKey = applySeed;
    seedEditor_.onFocusLost = applySeed;
    addChildComponent(seedEditor_);

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
    for (auto* label : {&energyLabel_, &creativityLabel_}) {
        label->setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        addChildComponent(*label);
    }

    for (size_t i = 0; i < slotButtons_.size(); ++i) {
        auto& button = slotButtons_[i];
        button.setButtonText(juce::String(static_cast<int>(i) + 1));
        button.setComponentID("slot" + juce::String(static_cast<int>(i) + 1));
        button.setClickingTogglesState(false);
        button.onClick = [this, i] { processor_.selectSlot(static_cast<int>(i) + 1); };
        addChildComponent(button);
    }

    infoLabel_.setComponentID("info");
    infoLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    addChildComponent(infoLabel_);

    openHubButton_.setComponentID("openHub");
    openHubButton_.onClick = [this] { processor_.showHub(); };
    addChildComponent(openHubButton_);

    muteButton_.setComponentID("mute");
    addChildComponent(muteButton_);

    octaveBox_.setComponentID("octave");
    for (int octave = -2; octave <= 2; ++octave) {
        octaveBox_.addItem(juce::String("Octave ") + (octave > 0 ? "+" : "") + juce::String(octave), octave + 3);
    }
    octaveBox_.setSelectedId(settings.octave + 3, juce::dontSendNotification);
    octaveBox_.onChange = [this] {
        auto next = processor_.instanceSettings();
        next.octave = octaveBox_.getSelectedId() - 3;
        processor_.setInstanceSettings(next);
        updateVoiceView();
    };
    addChildComponent(octaveBox_);

    rollView_.setComponentID("roll");
    addChildComponent(rollView_);

    generateButton_.onClick = [this] {
        processor_.generate();
        updateStatus();
    };
    addAndMakeVisible(generateButton_);

    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setMinimumHorizontalScale(1.0f); // the hub hint is long: wrap instead of squeezing
    addAndMakeVisible(statusLabel_);

    processor_.updateExport();
    updateStatus();  // chooses the layout and the size
    startTimerHz(4); // keep the exported tempo equal to the DAW tempo and the status line current
}

PlaceholderEditor::~PlaceholderEditor() {
    stopTimer();
}

void PlaceholderEditor::updateStatus() {
    using mm::core::GroupStatus;
    juce::String group = groupStatusText(processor_.groupStatus(), processor_.groupVoices());
    takeOverButton_.setVisible(processor_.groupStatus() == GroupStatus::HubOffered);
    const auto role = processor_.instanceSettings().role;
    const int roleId = role == mm::core::InstanceRole::Hub ? 2 : role == mm::core::InstanceRole::Voice ? 3 : 1;
    if (roleBox_.getSelectedId() != roleId) {
        roleBox_.setSelectedId(roleId, juce::dontSendNotification); // e.g. a voice that took over the hub
    }
    const bool isVoice = role == mm::core::InstanceRole::Voice;
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
        statusLabel_.setText("No hub to generate for this voice", juce::dontSendNotification);
        break;
    case GenerationStatus::Forwarded:
        statusLabel_.setText("Asked the hub to generate", juce::dontSendNotification);
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

void PlaceholderEditor::applyLayout(bool voiceLayout) {
    voiceLayout_ = voiceLayout;
    styleBox_.setVisible(!voiceLayout);
    muteButton_.setVisible(voiceLayout);
    octaveBox_.setVisible(voiceLayout);
    openHubButton_.setVisible(voiceLayout);
    rollView_.setVisible(voiceLayout);
    dragHandle_->setVisible(voiceLayout);
    for (auto* component : std::initializer_list<juce::Component*>{&keyBox_, &scaleBox_, &barsBox_, &seedEditor_,
                                                                   &randomButton_, &energySlider_, &creativitySlider_,
                                                                   &energyLabel_, &creativityLabel_, &infoLabel_}) {
        component->setVisible(!voiceLayout);
    }
    for (auto& button : slotButtons_) {
        button.setVisible(!voiceLayout);
    }
    for (auto& row : rows_) {
        row->setVisible(!voiceLayout);
    }
    // The voice UI is compact (SPEC 8.1); the hub UI has the size of the standard window (SPEC 8.1).
    setSize(voiceLayout ? 600 : 1000, voiceLayout ? 320 : 640);
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

void PlaceholderEditor::rebuildRows(size_t count) {
    rows_.clear();
    for (size_t i = 0; i < count; ++i) {
        auto row = std::make_unique<VoiceRow>(processor_, static_cast<int>(i) + 1);
        addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
    }
    shownVoices_.clear();
    resized();
}

void PlaceholderEditor::updateFullView() {
    syncFields(processor_.instanceSettings());

    // Slot strip: a filled slot is lighter, the selected one has the accent colour.
    const auto used = processor_.slotUsage();
    const int selected = processor_.activeSlot();
    for (size_t i = 0; i < slotButtons_.size(); ++i) {
        auto& button = slotButtons_[i];
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
            rows_[i]->setView(i < views.size() ? &views[i] : nullptr, i == 0 ? "Bass" : "Melody");
        }
    }
    for (auto& row : rows_) {
        row->updateMuted();
    }

    const auto info = processor_.playingSlotInfo();
    if (!(info == shownInfo_) || infoLabel_.getText().isEmpty()) {
        shownInfo_ = info;
        if (!info.filled) {
            infoLabel_.setText("Slot " + juce::String(info.slot) + ": empty. Generate fills it.",
                               juce::dontSendNotification);
        } else {
            const auto scale = std::string_view(info.scaleId);
            infoLabel_.setText("Slot " + juce::String(info.slot) + ": " + juce::String(info.styleId) + ", " +
                                   kKeyNames[info.root % 12] + " " + scaleLabel(scale).toLowerCase() + ", " +
                                   juce::String(static_cast<int>(info.lengthBars)) + " bars, seed " +
                                   juce::String(std::to_string(info.seed)) + " (winner " +
                                   juce::String(std::to_string(info.winnerSeed)) + ")",
                               juce::dontSendNotification);
        }
    }
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
    muteButton_.setButtonText(mutedByHub ? "Mute (by hub)" : "Mute");
    rollView_.setDimmed(processor_.mutedByOwnSwitch() || mutedByHub);
    const int octaveId = processor_.instanceSettings().octave + 3;
    if (octaveBox_.getSelectedId() != octaveId) {
        octaveBox_.setSelectedId(octaveId, juce::dontSendNotification);
    }
    auto view = processor_.playingVoice();
    if (!view.sameSource(shown_) || shown_.origin == 0 /* nothing shown yet */) {
        shown_ = view;
        rollView_.setAccent(view.voiceName == "Melody" ? juce::Colour(0xffe0a458) : juce::Colour(0xff4fc3a1));
        rollView_.setContent(mm::core::layoutRoll(view.notes, view.lengthTicks),
                             "Slot " + juce::String(view.slot) + " - " + view.voiceName, !view.hasPattern);
    }
}

void PlaceholderEditor::timerCallback() {
    updateStatus();
    processor_.updateExport();
}

void PlaceholderEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1b1b1f));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawText(getAudioProcessor()->getName(), getLocalBounds(), juce::Justification::centred);
}

void PlaceholderEditor::resized() {
    auto area = getLocalBounds();
    if (voiceLayout_) {
        dragHandle_->setBounds(area.removeFromBottom(56).reduced(16));
    }
    auto roles = area.removeFromTop(48).reduced(16, 10);
    takeOverButton_.setBounds(roles.removeFromRight(120));
    roles.removeFromRight(8);
    followBox_.setBounds(roles.removeFromRight(150));
    outputBox_.setBounds(followBox_.getBounds()); // never visible together
    roles.removeFromRight(8);
    voiceBox_.setBounds(roles.removeFromRight(120));
    roles.removeFromRight(8);
    roleBox_.setBounds(roles.removeFromLeft(voiceLayout_ ? roles.getWidth() : 150));

    auto top = area.removeFromTop(48).reduced(16, 10);
    generateButton_.setBounds(top.removeFromRight(120));
    top.removeFromRight(8);
    if (voiceLayout_) {
        openHubButton_.setBounds(top.removeFromRight(110));
        top.removeFromRight(8);
        octaveBox_.setBounds(top.removeFromRight(130));
        top.removeFromRight(8);
        muteButton_.setBounds(top.removeFromLeft(130));
        statusLabel_.setBounds(area.removeFromBottom(40).reduced(16, 0));
        rollView_.setBounds(area.reduced(16, 4));
        return;
    }

    // Hub UI: the settings of "Generate" in one row, then energy and creativity, the slot strip, the voices.
    randomButton_.setBounds(top.removeFromRight(110));
    top.removeFromRight(8);
    seedEditor_.setBounds(top.removeFromRight(150));
    top.removeFromRight(8);
    barsBox_.setBounds(top.removeFromRight(90));
    top.removeFromRight(8);
    scaleBox_.setBounds(top.removeFromRight(190));
    top.removeFromRight(8);
    keyBox_.setBounds(top.removeFromRight(110));
    top.removeFromRight(8);
    styleBox_.setBounds(top);

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
        button.setBounds(strip.removeFromLeft(buttonWidth).reduced(2, 0));
    }

    statusLabel_.setBounds(area.removeFromBottom(40).reduced(16, 0));
    infoLabel_.setBounds(area.removeFromBottom(28).reduced(16, 0));
    if (!rows_.empty()) {
        const int rowHeight = area.getHeight() / static_cast<int>(rows_.size());
        for (auto& row : rows_) {
            row->setBounds(area.removeFromTop(rowHeight));
        }
    }
}

} // namespace mm::plugin
