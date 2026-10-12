#pragma once

#include "plugin/AiConnection.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/MidiMaidLookAndFeel.h"

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
        std::vector<juce::Component*> controls; ///< the last one gets the rest of the width (or `fixedWidth`)
        std::vector<int> widths;                ///< widths of all but the last control
        int height = 34;
        bool indent = false;  ///< no label: starts in the column of the controls
        bool section = false; ///< starts a new group: more space and a thin line above
        int stretch = -1;     ///< the control that takes the rest of the width; -1: the last one
        int fixedWidth = 0;   ///< > 0: the last control has this width instead of the rest
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

    MidiMaidLookAndFeel look_; // first: the children use it until the end
    AiConnection& connection_;
    std::function<GlobalSettingsStore&()> settings_;
    std::string selected_; ///< id of the provider shown; empty: off
    mm::ai::ModelsConfig models_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

    juce::Label transposeLabel_, transposeHint_;
    juce::ComboBox transposeModeBox_, transposeBehaviorBox_, transposeTimingBox_, transposeTargetBox_,
        transposeReferenceBox_, transposeChannelBox_;
    juce::ToggleButton transposeExportToggle_, midiThruToggle_;
    juce::Label rollViewLabel_;
    juce::ToggleButton rollFoldToggle_, rollGuidesToggle_, rollMarkersToggle_; ///< the aids of the piano roll (D-232)
    void changeTranspose(const std::function<void(mm::core::TransposeSettings&)>& change);
    void updateTranspose();
    juce::Label languageLabel_, languageHint_;
    juce::ComboBox languageBox_;
    juce::Label noteNamingLabel_;
    juce::ComboBox noteNamingBox_;
    void fillReferenceNotes();
    juce::Label drumMapLabel_, drumMapHint_;
    juce::TextEditor drumMapEditor_;
    juce::Label providerLabel_, keyLabel_, urlLabel_, modelLabel_, timeoutLabel_, tokensLabel_, levelLabel_,
        consentLabel_, logLabel_;
    juce::ComboBox providerBox_, modelBox_, levelBox_;
    juce::TextEditor keyEditor_, urlEditor_, timeoutEditor_, tokensEditor_;
    juce::TextButton keySaveButton_, keyRemoveButton_, loadModelsButton_, testButton_, closeButton_;
    juce::Label keyStatus_, modelStatus_, testStatus_, consentList_, logHint_;
    juce::ToggleButton consentToggle_, logPromptsToggle_;
    bool keyKnown_ = false; ///< the keychain said there is a key for the selected provider
    bool updating_ = false; ///< the fields are being filled: no changes to save
    std::vector<Row> rows_;
    std::vector<int> dividers_; ///< y of the lines between the groups, set by `resized`

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};

} // namespace mm::plugin
