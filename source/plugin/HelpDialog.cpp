#include "plugin/HelpDialog.h"

#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

namespace keys = mm::core::text;

namespace {

constexpr int kWidth = 680;
constexpr int kHeight = 600;
constexpr int kPad = 16;
constexpr int kGap = 10;
constexpr int kControl = 34;
constexpr std::array<HelpTab, 4> kTabs{HelpTab::General, HelpTab::Live, HelpTab::Logic, HelpTab::Other};
constexpr std::array<const char*, 4> kTabIds{"tabGeneral", "tabLive", "tabLogic", "tabOther"};

} // namespace

HelpTab helpTabFor(HostKind host) {
    switch (host) {
    case HostKind::AbletonLive:
        return HelpTab::Live;
    case HostKind::Logic:
        return HelpTab::Logic;
    case HostKind::Reaper:
    case HostKind::Bitwig:
        return HelpTab::Other;
    case HostKind::Unknown:
        break;
    }
    return HelpTab::General;
}

std::string_view helpTextKey(HelpTab tab) {
    switch (tab) {
    case HelpTab::General:
        return keys::kHelpTextGeneral;
    case HelpTab::Live:
        return keys::kHelpTextLive;
    case HelpTab::Logic:
        return keys::kHelpTextLogic;
    case HelpTab::Other:
        return keys::kHelpTextOther;
    }
    return keys::kHelpTextGeneral;
}

std::string_view helpTabKey(HelpTab tab) {
    switch (tab) {
    case HelpTab::General:
        return keys::kHelpTabGeneral;
    case HelpTab::Live:
        return keys::kHelpTabLive;
    case HelpTab::Logic:
        return keys::kHelpTabLogic;
    case HelpTab::Other:
        return keys::kHelpTabOther;
    }
    return keys::kHelpTabGeneral;
}

HelpDialog::HelpDialog(HostKind host) {
    setLookAndFeel(&look_);
    for (size_t i = 0; i < kTabs.size(); ++i) {
        auto& button = tabButtons_[i];
        button.setComponentID(kTabIds[i]);
        button.setButtonText(tr(helpTabKey(kTabs[i])));
        button.setClickingTogglesState(true);
        button.setRadioGroupId(1);
        button.onClick = [this, tab = kTabs[i]] { showTab(tab); };
        addAndMakeVisible(button);
    }
    text_.setComponentID("text");
    text_.setMultiLine(true, true);
    text_.setReadOnly(true);
    text_.setCaretVisible(false);
    text_.setScrollbarsShown(true);
    text_.setFont(juce::FontOptions(15.0f));
    text_.setIndents(12, 12);
    addAndMakeVisible(text_);
    closeButton_.setComponentID("close");
    closeButton_.setButtonText(tr(keys::kButtonClose));
    closeButton_.onClick = [this] {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) {
            window->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);
    setSize(kWidth, kHeight);
    showTab(helpTabFor(host));
}

HelpDialog::~HelpDialog() {
    setLookAndFeel(nullptr);
}

void HelpDialog::showTab(HelpTab tab) {
    tab_ = tab;
    for (size_t i = 0; i < kTabs.size(); ++i) {
        tabButtons_[i].setToggleState(kTabs[i] == tab, juce::dontSendNotification);
    }
    text_.setText(tr(helpTextKey(tab)), false);
    text_.moveCaretToTop(false);
}

void HelpDialog::resized() {
    auto area = getLocalBounds().reduced(kPad);
    auto tabs = area.removeFromTop(kControl);
    const int width = (tabs.getWidth() - kGap * 3) / 4;
    for (auto& button : tabButtons_) {
        button.setBounds(tabs.removeFromLeft(width));
        tabs.removeFromLeft(kGap);
    }
    area.removeFromTop(kGap);
    closeButton_.setBounds(area.removeFromBottom(kControl).removeFromRight(110));
    area.removeFromBottom(kGap);
    text_.setBounds(area);
}

void HelpDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(MidiMaidLookAndFeel::kBackground));
}

void HelpDialog::show(HostKind host) {
    static juce::Component::SafePointer<juce::DialogWindow> open;
    if (open != nullptr) {
        open->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new HelpDialog(host));
    options.dialogTitle = tr(keys::kHelpTitle);
    options.dialogBackgroundColour = juce::Colour(MidiMaidLookAndFeel::kBackground);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    open = options.launchAsync();
}

} // namespace mm::plugin
