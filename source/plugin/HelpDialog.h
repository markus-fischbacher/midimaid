#pragma once

#include "plugin/HostCapabilities.h"
#include "plugin/MidiMaidLookAndFeel.h"

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string_view>

namespace mm::plugin {

/// The pages of the help (SPEC 6.5): the principle and the set-up for each host.
enum class HelpTab { General, Live, Logic, Other };

/// The page that fits the detected host: Live and Logic have their own, Bitwig and Reaper the page with the sandbox
/// notes, an unknown host starts with the principle.
HelpTab helpTabFor(HostKind host);
/// The text key of the page's body and of its tab.
std::string_view helpTextKey(HelpTab tab);
std::string_view helpTabKey(HelpTab tab);

/// The help dialog "Hilfe" (SPEC 6.5): the set-up of hub and voices step by step, one page per host, opened on the page
/// of the host the plug-in runs in.
class HelpDialog : public juce::Component {
public:
    explicit HelpDialog(HostKind host);
    ~HelpDialog() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    HelpTab tab() const { return tab_; }
    void showTab(HelpTab tab);

    /// Opens the dialog in a window of its own, or brings the open one to the front. Message thread.
    static void show(HostKind host);

private:
    MidiMaidLookAndFeel look_;
    HelpTab tab_ = HelpTab::General;
    std::array<juce::TextButton, 4> tabButtons_;
    juce::TextEditor text_;
    juce::TextButton closeButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelpDialog)
};

} // namespace mm::plugin
