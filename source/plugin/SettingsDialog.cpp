#include "plugin/SettingsDialog.h"

#include "core/TextKeys.h"
#include "plugin/AiText.h"
#include "plugin/EmbeddedTranslation.h"

#include <algorithm>

namespace mm::plugin {

namespace keys = mm::core::text;

namespace {

constexpr int kWidth = 660;
constexpr int kLabelWidth = 170;
constexpr int kGap = 8;

juce::String toJuce(const std::string& text) {
    return juce::String::fromUTF8(text.c_str());
}

mm::core::AiProviderSettings settingsOf(const mm::core::AiSettings& ai, const std::string& id) {
    const auto it = ai.providers.find(id);
    return it != ai.providers.end() ? it->second : mm::core::AiProviderSettings{};
}

} // namespace

juce::Label& SettingsDialog::makeLabel(juce::Label& label, std::string_view key) {
    label.setText(tr(key), juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    addAndMakeVisible(label);
    return label;
}

SettingsDialog::SettingsDialog(AiConnection& connection, std::function<GlobalSettingsStore&()> settings)
    : connection_(connection), settings_(std::move(settings)) {
    models_ = connection_.models();
    const auto ai = settings_().get().ai;
    selected_ = models_.find(ai.activeProvider) != nullptr ? ai.activeProvider : std::string();

    makeLabel(providerLabel_, keys::kLabelProvider);
    makeLabel(keyLabel_, keys::kLabelKey);
    makeLabel(urlLabel_, keys::kLabelBaseUrl);
    makeLabel(modelLabel_, keys::kLabelModel);
    makeLabel(timeoutLabel_, keys::kLabelTimeout);
    makeLabel(tokensLabel_, keys::kLabelMaxTokens);
    makeLabel(levelLabel_, keys::kLabelSchemaLevel);
    makeLabel(consentLabel_, keys::kLabelConsent);
    makeLabel(drumMapLabel_, keys::kLabelDrumMap);
    drumMapHint_.setText(tr(keys::kDrumMapHint), juce::dontSendNotification);
    drumMapHint_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    addAndMakeVisible(drumMapHint_);
    drumMapEditor_.setComponentID("drumMap");
    drumMapEditor_.setInputRestrictions(400);
    drumMapEditor_.setText(toJuce(settings_().get().drumMap), false);
    const auto applyDrumMap = [this] {
        const auto text = drumMapEditor_.getText().toStdString();
        settings_().update([&](mm::core::GlobalSettings& value) { value.drumMap = text; }); // sanitised there
        drumMapEditor_.setText(toJuce(settings_().get().drumMap), false);
    };
    drumMapEditor_.onReturnKey = applyDrumMap;
    drumMapEditor_.onFocusLost = applyDrumMap;
    addAndMakeVisible(drumMapEditor_);

    providerBox_.setComponentID("provider");
    providerBox_.onChange = [this] {
        if (updating_) {
            return;
        }
        const int id = providerBox_.getSelectedId();
        const std::string chosen = id <= 1 ? std::string() : models_.providers[static_cast<size_t>(id - 2)].id;
        changeAi([&](mm::core::AiSettings& value) { value.activeProvider = chosen; });
        selectProvider(chosen);
    };
    addAndMakeVisible(providerBox_);

    keyEditor_.setComponentID("key");
    keyEditor_.setPasswordCharacter(0x2022);
    keyEditor_.setTextToShowWhenEmpty(tr(keys::kKeyHint), juce::Colours::white.withAlpha(0.45f));
    keyEditor_.setInputRestrictions(512);
    keyEditor_.onReturnKey = [this] { keySaveButton_.onClick(); };
    addAndMakeVisible(keyEditor_);

    keySaveButton_.setComponentID("keySave");
    keySaveButton_.setButtonText(tr(keys::kButtonKeySave));
    keySaveButton_.onClick = [this] {
        const auto text = keyEditor_.getText().trim().toStdString();
        if (selected_.empty() || text.empty()) {
            return;
        }
        const auto alive = alive_;
        const auto id = selected_;
        connection_.saveKey(id, text, [this, alive, id](bool ok) {
            if (!*alive) {
                return;
            }
            if (ok) {
                keyEditor_.clear(); // the key is in the keychain now: it does not stay on the screen
                keyKnown_ = true;
            }
            keyStatus_.setText(tr(ok ? keys::kKeySaved : keys::kKeySaveFailed), juce::dontSendNotification);
            if (ok && id == selected_) {
                updateKeyStatus();
            }
        });
    };
    addAndMakeVisible(keySaveButton_);

    keyRemoveButton_.setComponentID("keyRemove");
    keyRemoveButton_.setButtonText(tr(keys::kButtonKeyRemove));
    keyRemoveButton_.onClick = [this] {
        if (selected_.empty()) {
            return;
        }
        const auto alive = alive_;
        const auto id = selected_;
        connection_.removeKey(id, [this, alive, id](bool ok) {
            if (!*alive) {
                return;
            }
            keyEditor_.clear();
            if (ok) {
                keyKnown_ = false;
                keyStatus_.setText(tr(keys::kKeyRemoved), juce::dontSendNotification);
                if (id != selected_) {
                    return;
                }
            }
            updateKeyStatus();
        });
    };
    addAndMakeVisible(keyRemoveButton_);

    keyStatus_.setComponentID("keyStatus");
    keyStatus_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    addAndMakeVisible(keyStatus_);

    urlEditor_.setComponentID("url");
    urlEditor_.setInputRestrictions(500);
    const auto applyUrl = [this] {
        if (updating_) {
            return;
        }
        const auto text = urlEditor_.getText().trim().toStdString();
        changeProvider([&](mm::core::AiProviderSettings& value) {
            value.baseUrl = entry() != nullptr && text == entry()->baseUrl ? std::string() : text;
        });
    };
    urlEditor_.onReturnKey = applyUrl;
    urlEditor_.onFocusLost = applyUrl;
    addAndMakeVisible(urlEditor_);

    modelBox_.setComponentID("model");
    modelBox_.setEditableText(true);
    modelBox_.onChange = [this] {
        if (updating_) {
            return;
        }
        const auto text = modelBox_.getText().trim().toStdString();
        changeProvider([&](mm::core::AiProviderSettings& value) {
            value.model = entry() != nullptr && text == entry()->recommendedModel ? std::string() : text;
        });
    };
    addAndMakeVisible(modelBox_);

    loadModelsButton_.setComponentID("loadModels");
    loadModelsButton_.setButtonText(tr(keys::kButtonLoadModels));
    loadModelsButton_.onClick = [this] {
        if (selected_.empty()) {
            return;
        }
        const auto alive = alive_;
        const auto id = selected_;
        connection_.loadModels(id, [this, alive, id](std::vector<std::string> list) {
            if (!*alive || id != selected_) {
                return;
            }
            const auto current = modelBox_.getText();
            updating_ = true;
            modelBox_.clear(juce::dontSendNotification);
            int itemId = 1;
            for (const auto& model : list) {
                modelBox_.addItem(toJuce(model), itemId++);
            }
            modelBox_.setText(current, juce::dontSendNotification);
            updating_ = false;
            modelStatus_.setText(list.empty() ? tr(keys::kModelsNone)
                                              : tr(keys::kModelsLoaded, {{"n", std::to_string(list.size())}}),
                                 juce::dontSendNotification);
        });
    };
    addAndMakeVisible(loadModelsButton_);

    modelStatus_.setComponentID("modelStatus");
    modelStatus_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    addAndMakeVisible(modelStatus_);

    for (auto* editor : {&timeoutEditor_, &tokensEditor_}) {
        editor->setInputRestrictions(6, "0123456789");
        addAndMakeVisible(*editor);
    }
    timeoutEditor_.setComponentID("timeout");
    tokensEditor_.setComponentID("maxTokens");
    const auto applyNumbers = [this] {
        if (updating_) {
            return;
        }
        const int timeout = timeoutEditor_.getText().getIntValue();
        const int tokens = tokensEditor_.getText().getIntValue();
        changeProvider([&](mm::core::AiProviderSettings& value) {
            value.timeoutSeconds = timeout;
            value.maxTokens = tokens;
        });
        updateFields(); // shows what was kept after the limits
    };
    for (auto* editor : {&timeoutEditor_, &tokensEditor_}) {
        editor->onReturnKey = applyNumbers;
        editor->onFocusLost = applyNumbers;
    }

    levelBox_.setComponentID("level");
    levelBox_.addItem(tr(keys::kLevelDefault), 1);
    levelBox_.addItem(tr(keys::kLevelEnforced), 2);
    levelBox_.addItem(tr(keys::kLevelJson), 3);
    levelBox_.addItem(tr(keys::kLevelPrompt), 4);
    levelBox_.onChange = [this] {
        if (updating_) {
            return;
        }
        static const char* const names[] = {"", "enforced", "json", "prompt"};
        const auto level = std::string(names[std::clamp(levelBox_.getSelectedId() - 1, 0, 3)]);
        changeProvider([&](mm::core::AiProviderSettings& value) { value.schemaLevel = level; });
    };
    addAndMakeVisible(levelBox_);

    testButton_.setComponentID("test");
    testButton_.setButtonText(tr(keys::kButtonTestConnection));
    testButton_.onClick = [this] {
        if (selected_.empty()) {
            return;
        }
        testStatus_.setText(tr(keys::kTestRunning), juce::dontSendNotification);
        const auto alive = alive_;
        const auto id = selected_;
        const auto configured = entry() != nullptr ? entry()->schemaSupport : mm::ai::SchemaSupport::JsonOnly;
        connection_.testConnection(id, [this, alive, id, configured](mm::ai::ConnectionStatus status) {
            if (!*alive || id != selected_) {
                return;
            }
            if (!status.ok) {
                testStatus_.setText(tr(keys::kTestFailed, {{"reason", aiStatusText(status.status, 0).toStdString()}}),
                                    juce::dontSendNotification);
            } else if (status.observed != configured) {
                testStatus_.setText(
                    tr(keys::kTestOkLowered, {{"level", schemaLevelText(status.observed).toStdString()}}),
                    juce::dontSendNotification);
            } else {
                testStatus_.setText(tr(keys::kTestOk), juce::dontSendNotification);
            }
        });
    };
    addAndMakeVisible(testButton_);

    testStatus_.setComponentID("testStatus");
    testStatus_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.8f));
    addAndMakeVisible(testStatus_);

    consentToggle_.setComponentID("consent");
    consentToggle_.onClick = [this] {
        if (selected_.empty() || updating_) {
            return;
        }
        const bool allow = consentToggle_.getToggleState();
        const auto id = selected_;
        changeAi([&](mm::core::AiSettings& value) {
            if (allow) {
                value.cloudConsent.insert(id);
            } else {
                value.cloudConsent.erase(id);
            }
        });
        updateConsent();
    };
    addAndMakeVisible(consentToggle_);

    consentList_.setComponentID("consentList");
    consentList_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    addAndMakeVisible(consentList_);

    logPromptsToggle_.setComponentID("logPrompts");
    logPromptsToggle_.setButtonText(tr(keys::kToggleLogPrompts));
    logPromptsToggle_.onClick = [this] {
        if (updating_) {
            return;
        }
        const bool on = logPromptsToggle_.getToggleState();
        changeAi([&](mm::core::AiSettings& value) { value.logPrompts = on; });
    };
    addAndMakeVisible(logPromptsToggle_);

    logHint_.setText(tr(keys::kLogHint), juce::dontSendNotification);
    logHint_.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.5f));
    addAndMakeVisible(logHint_);

    closeButton_.setComponentID("close");
    closeButton_.setButtonText(tr(keys::kButtonClose));
    closeButton_.onClick = [this] {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) {
            window->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);

    rows_ = {{&providerLabel_, {&providerBox_}, {}, 28},
             {&keyLabel_, {&keyEditor_, &keySaveButton_, &keyRemoveButton_}, {100, 100}, 28},
             {nullptr, {&keyStatus_}, {}, 22},
             {&urlLabel_, {&urlEditor_}, {}, 28},
             {&modelLabel_, {&modelBox_, &loadModelsButton_}, {240}, 28},
             {nullptr, {&modelStatus_}, {}, 22},
             {&timeoutLabel_, {&timeoutEditor_}, {}, 28},
             {&tokensLabel_, {&tokensEditor_}, {}, 28},
             {&levelLabel_, {&levelBox_}, {}, 28},
             {nullptr, {&testButton_, &testStatus_}, {170}, 28},
             {&consentLabel_, {&consentToggle_}, {}, 28},
             {nullptr, {&consentList_}, {}, 22},
             {nullptr, {&logPromptsToggle_}, {}, 28},
             {nullptr, {&logHint_}, {}, 22},
             {&drumMapLabel_, {&drumMapEditor_}, {}, 28},
             {nullptr, {&drumMapHint_}, {}, 22},
             {nullptr, {&closeButton_}, {}, 30}};

    fillProviders();
    selectProvider(selected_);
    setSize(kWidth, 100);
    updateVisibility();
}

SettingsDialog::~SettingsDialog() {
    *alive_ = false;
}

const mm::ai::ProviderEntry* SettingsDialog::entry() const {
    return selected_.empty() ? nullptr : models_.find(selected_);
}

void SettingsDialog::fillProviders() {
    updating_ = true;
    providerBox_.clear(juce::dontSendNotification);
    providerBox_.addItem(tr(keys::kProviderOffline), 1);
    int id = 2;
    for (const auto& provider : models_.providers) {
        providerBox_.addItem(toJuce(provider.name), id++);
    }
    updating_ = false;
}

void SettingsDialog::selectProvider(const std::string& id) {
    selected_ = id;
    keyKnown_ = false;
    keyEditor_.clear();
    modelStatus_.setText({}, juce::dontSendNotification);
    testStatus_.setText({}, juce::dontSendNotification);
    keyStatus_.setText({}, juce::dontSendNotification);
    updateFields();
    updateKeyStatus();
    updateConsent();
    updateVisibility();
    if (!id.empty()) {
        const auto alive = alive_;
        connection_.hasKey(id, [this, alive, id](bool present) {
            if (*alive && id == selected_) {
                keyKnown_ = present;
                updateKeyStatus();
            }
        });
    }
}

void SettingsDialog::updateFields() {
    updating_ = true;
    const auto ai = settings_().get().ai;
    int providerIndex = 1;
    for (size_t i = 0; i < models_.providers.size(); ++i) {
        if (models_.providers[i].id == selected_) {
            providerIndex = static_cast<int>(i) + 2;
        }
    }
    providerBox_.setSelectedId(providerIndex, juce::dontSendNotification);
    logPromptsToggle_.setToggleState(ai.logPrompts, juce::dontSendNotification);

    const auto* current = entry();
    if (current != nullptr) {
        const auto chosen = settingsOf(ai, selected_);
        urlEditor_.setText(toJuce(chosen.baseUrl.empty() ? current->baseUrl : chosen.baseUrl), false);
        modelBox_.clear(juce::dontSendNotification);
        int itemId = 1;
        for (const auto& model : current->models) {
            modelBox_.addItem(toJuce(model), itemId++);
        }
        modelBox_.setText(toJuce(chosen.model.empty() ? current->recommendedModel : chosen.model),
                          juce::dontSendNotification);
        timeoutEditor_.setText(chosen.timeoutSeconds > 0 ? juce::String(chosen.timeoutSeconds) : juce::String(), false);
        timeoutEditor_.setTextToShowWhenEmpty(juce::String(current->timeoutSeconds),
                                              juce::Colours::white.withAlpha(0.45f));
        tokensEditor_.setText(chosen.maxTokens > 0 ? juce::String(chosen.maxTokens) : juce::String(), false);
        tokensEditor_.setTextToShowWhenEmpty(juce::String(mm::core::kDefaultMaxTokens),
                                             juce::Colours::white.withAlpha(0.45f));
        int level = 1;
        if (chosen.schemaLevel == "enforced") {
            level = 2;
        } else if (chosen.schemaLevel == "json") {
            level = 3;
        } else if (chosen.schemaLevel == "prompt") {
            level = 4;
        }
        levelBox_.setSelectedId(level, juce::dontSendNotification);
    }
    updating_ = false;
}

void SettingsDialog::updateKeyStatus() {
    const auto* current = entry();
    if (current == nullptr) {
        keyStatus_.setText({}, juce::dontSendNotification);
        return;
    }
    juce::String text;
    if (keyKnown_) {
        text = tr(connection_.keysPersist() ? keys::kKeyStored : keys::kKeyMemoryOnly);
    } else {
        text = tr(current->keyRequired ? keys::kKeyMissing : keys::kKeyNotRequired);
    }
    keyStatus_.setText(text, juce::dontSendNotification);
}

void SettingsDialog::updateConsent() {
    const auto ai = settings_().get().ai;
    const auto* current = entry();
    updating_ = true;
    if (current != nullptr && current->cloud) {
        consentToggle_.setButtonText(tr(keys::kConsentToggle, {{"provider", current->name}}));
        consentToggle_.setToggleState(ai.cloudConsent.count(current->id) != 0, juce::dontSendNotification);
        consentToggle_.setEnabled(true);
    } else if (current != nullptr) {
        consentToggle_.setButtonText(tr(keys::kConsentLocal, {{"provider", current->name}}));
        consentToggle_.setToggleState(false, juce::dontSendNotification);
        consentToggle_.setEnabled(false);
    }
    std::string list;
    for (const auto& provider : models_.providers) {
        if (ai.cloudConsent.count(provider.id) != 0) {
            list += (list.empty() ? "" : ", ") + provider.name;
        }
    }
    consentList_.setText(list.empty() ? tr(keys::kConsentNone) : tr(keys::kConsentList, {{"list", list}}),
                         juce::dontSendNotification);
    updating_ = false;
}

void SettingsDialog::updateVisibility() {
    const bool any = entry() != nullptr;
    const bool expert = settings_().get().expert;
    for (auto* component : std::initializer_list<juce::Component*>{
             &keyLabel_,    &keyEditor_,    &keySaveButton_, &keyRemoveButton_, &keyStatus_,
             &urlLabel_,    &urlEditor_,    &modelLabel_,    &modelBox_,        &loadModelsButton_,
             &modelStatus_, &timeoutLabel_, &timeoutEditor_, &tokensLabel_,     &tokensEditor_,
             &testButton_,  &testStatus_,   &consentLabel_,  &consentToggle_,   &consentList_}) {
        component->setVisible(any);
    }
    levelLabel_.setVisible(any && expert);
    levelBox_.setVisible(any && expert);
    layoutRows();
}

void SettingsDialog::changeProvider(const std::function<void(mm::core::AiProviderSettings&)>& change) {
    if (selected_.empty()) {
        return;
    }
    const auto id = selected_;
    changeAi([&](mm::core::AiSettings& ai) { change(ai.providers[id]); });
}

void SettingsDialog::changeAi(const std::function<void(mm::core::AiSettings&)>& change) {
    settings_().update([&](mm::core::GlobalSettings& settings) { change(settings.ai); });
    connection_.refresh();
}

void SettingsDialog::layoutRows() {
    int height = 16;
    for (auto& row : rows_) {
        const bool visible = row.controls.front()->isVisible();
        if (visible) {
            height += row.height + 6;
        }
    }
    setSize(kWidth, height + 10);
    resized();
}

void SettingsDialog::resized() {
    int y = 16;
    for (auto& row : rows_) {
        if (!row.controls.front()->isVisible()) {
            continue;
        }
        juce::Rectangle<int> area(16, y, getWidth() - 32, row.height);
        if (row.label != nullptr) {
            row.label->setBounds(area.removeFromLeft(kLabelWidth));
        } else {
            area.removeFromLeft(kLabelWidth);
        }
        if (row.controls.size() == 1 && row.label == nullptr) {
            area = {16, y, getWidth() - 32, row.height}; // a single control without label uses the whole row
            if (row.controls.front() == &keyStatus_ || row.controls.front() == &modelStatus_ ||
                row.controls.front() == &consentList_ || row.controls.front() == &logHint_) {
                area.removeFromLeft(kLabelWidth); // status lines line up with the controls
            }
        }
        if (row.controls.front() == &closeButton_) {
            area = {getWidth() - 16 - 120, y, 120, row.height};
        }
        for (size_t i = 0; i < row.controls.size(); ++i) {
            if (i + 1 < row.controls.size()) {
                row.controls[i]->setBounds(area.removeFromLeft(row.widths[i]));
                area.removeFromLeft(kGap);
            } else {
                row.controls[i]->setBounds(area);
            }
        }
        y += row.height + 6;
    }
}

void SettingsDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1b1b1f));
}

void SettingsDialog::show() {
    static juce::Component::SafePointer<juce::DialogWindow> open;
    if (open != nullptr) {
        open->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new SettingsDialog(AiConnection::instance(), [] { return std::ref(globalSettings()); }));
    options.dialogTitle = tr(keys::kSettingsTitle);
    options.dialogBackgroundColour = juce::Colour(0xff1b1b1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    open = options.launchAsync();
}

} // namespace mm::plugin
