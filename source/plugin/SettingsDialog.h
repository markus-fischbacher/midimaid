#pragma once

#include "plugin/AiConnection.h"
#include "plugin/GlobalSettingsStore.h"

#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

namespace mm::plugin {

/// The settings of the AI (SPEC 8.4): provider, key (masked, kept in the keychain only), address, model, time limit,
/// maximum tokens, structured output level (expert), connection test, consent per cloud provider and "Prompts
/// protokollieren". Every change is saved to the settings file at once and applied by `AiConnection`; there is no OK
/// button. The dialog does not depend on an editor: it works on the process wide settings and the connection.
class SettingsDialog : public juce::Component {
public:
    SettingsDialog(AiConnection& connection, std::function<GlobalSettingsStore&()> settings);
    ~SettingsDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    /// Opens the dialog in a window of its own, or brings the open one to the front. Message thread.
    static void show();

private:
    struct Row {
        juce::Component* label = nullptr;
        std::vector<juce::Component*> controls; ///< the last one gets the rest of the width
        std::vector<int> widths;                ///< widths of all but the last control
        int height = 28;
    };

    const mm::ai::ProviderEntry* entry() const;
    std::string selectedId() const { return selected_; }
    void fillProviders();
    void selectProvider(const std::string& id);
    void updateFields();
    void updateKeyStatus();
    void updateConsent();
    void updateVisibility();
    /// Changes the settings of the selected provider and applies them.
    void changeProvider(const std::function<void(mm::core::AiProviderSettings&)>& change);
    void changeAi(const std::function<void(mm::core::AiSettings&)>& change);
    void layoutRows();
    juce::Label& makeLabel(juce::Label& label, std::string_view key);

    AiConnection& connection_;
    std::function<GlobalSettingsStore&()> settings_;
    std::string selected_; ///< id of the provider shown; empty: off
    mm::ai::ModelsConfig models_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    juce::Label drumMapLabel_, drumMapHint_;
    juce::TextEditor drumMapEditor_;
    juce::Label providerLabel_, keyLabel_, urlLabel_, modelLabel_, timeoutLabel_, tokensLabel_, levelLabel_,
        consentLabel_;
    juce::ComboBox providerBox_, modelBox_, levelBox_;
    juce::TextEditor keyEditor_, urlEditor_, timeoutEditor_, tokensEditor_;
    juce::TextButton keySaveButton_, keyRemoveButton_, loadModelsButton_, testButton_, closeButton_;
    juce::Label keyStatus_, modelStatus_, testStatus_, consentList_, logHint_;
    juce::ToggleButton consentToggle_, logPromptsToggle_;
    bool keyKnown_ = false; ///< the keychain said there is a key for the selected provider
    bool updating_ = false; ///< the fields are being filled: no changes to save
    std::vector<Row> rows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};

} // namespace mm::plugin
