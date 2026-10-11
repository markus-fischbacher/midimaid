#include "plugin/EmbeddedTranslation.h"
#include "plugin/HelpDialog.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <string>

using namespace mm::plugin;

namespace {

juce::Component* findById(juce::Component& parent, const juce::String& id) {
    for (auto* component : parent.getChildren()) {
        if (component->getComponentID() == id) {
            return component;
        }
    }
    return nullptr;
}

juce::TextEditor& textOf(HelpDialog& dialog) {
    auto* text = dynamic_cast<juce::TextEditor*>(findById(dialog, "text"));
    REQUIRE(text != nullptr);
    return *text;
}

juce::TextButton& tabButton(HelpDialog& dialog, const juce::String& id) {
    auto* button = dynamic_cast<juce::TextButton*>(findById(dialog, id));
    REQUIRE(button != nullptr);
    return *button;
}

} // namespace

TEST_CASE("the help opens on the page of the detected host", "[help]") {
    CHECK(helpTabFor(HostKind::AbletonLive) == HelpTab::Live);
    CHECK(helpTabFor(HostKind::Logic) == HelpTab::Logic);
    CHECK(helpTabFor(HostKind::Bitwig) == HelpTab::Other);
    CHECK(helpTabFor(HostKind::Reaper) == HelpTab::Other);
    CHECK(helpTabFor(HostKind::Unknown) == HelpTab::General);

    juce::ScopedJuceInitialiser_GUI gui;
    for (const auto host : {HostKind::AbletonLive, HostKind::Logic, HostKind::Bitwig, HostKind::Unknown}) {
        HelpDialog dialog(host);
        CHECK(dialog.tab() == helpTabFor(host));
        CHECK(textOf(dialog).getText() == tr(helpTextKey(helpTabFor(host))));
        const std::pair<const char*, HelpTab> pages[] = {{"tabGeneral", HelpTab::General},
                                                         {"tabLive", HelpTab::Live},
                                                         {"tabLogic", HelpTab::Logic},
                                                         {"tabOther", HelpTab::Other}};
        for (const auto& [id, tab] : pages) {
            CHECK(tabButton(dialog, id).getToggleState() == (tab == helpTabFor(host))); // the open page is marked
        }
    }
}

TEST_CASE("every page has its own text and its own tab", "[help]") {
    std::set<std::string> texts;
    std::set<std::string> tabs;
    for (const auto tab : {HelpTab::General, HelpTab::Live, HelpTab::Logic, HelpTab::Other}) {
        const auto text = tr(helpTextKey(tab)).toStdString();
        CHECK(text.size() > 200); // a real page, not the key echoed back
        CHECK(text.find("help.text") == std::string::npos);
        texts.insert(text);
        tabs.insert(tr(helpTabKey(tab)).toStdString());
    }
    CHECK(texts.size() == 4);
    CHECK(tabs.size() == 4);
}

TEST_CASE("the pages say what the host needs", "[help]") {
    const auto live = tr(helpTextKey(HelpTab::Live)).toStdString();
    const auto logic = tr(helpTextKey(HelpTab::Logic)).toStdString();
    const auto other = tr(helpTextKey(HelpTab::Other)).toStdString();
    const auto general = tr(helpTextKey(HelpTab::General)).toStdString();
    CHECK(live.find("MIDI From") != std::string::npos);
    CHECK(live.find("Post FX") != std::string::npos);
    CHECK(live.find("Freeze") != std::string::npos);
    CHECK(logic.find("Record MIDI to Track Here") != std::string::npos);
    CHECK(logic.find("MidiMaid MIDI") != std::string::npos);
    CHECK(other.find("By Plug-in") != std::string::npos);
    CHECK(general.find("Hub") != std::string::npos);
    CHECK(general.find("Slot: eigener") != std::string::npos);
}

TEST_CASE("the tabs switch the page and only one is selected", "[help]") {
    juce::ScopedJuceInitialiser_GUI gui;
    HelpDialog dialog(HostKind::Unknown);
    const std::pair<const char*, HelpTab> pages[] = {
        {"tabGeneral", HelpTab::General}, {"tabLive", HelpTab::Live}, {"tabLogic", HelpTab::Logic},
        {"tabOther", HelpTab::Other}};
    for (const auto& [id, tab] : pages) {
        tabButton(dialog, id).triggerClick(); // asynchronous
        for (int i = 0; i < 100 && (dialog.tab() != tab || i < 2); ++i) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        }
        CHECK(dialog.tab() == tab);
        CHECK(textOf(dialog).getText() == tr(helpTextKey(tab)));
        for (const auto& [otherId, otherTab] : pages) {
            CHECK(tabButton(dialog, otherId).getToggleState() == (otherTab == tab));
        }
    }
}

TEST_CASE("the help is laid out inside its window and uses the look of the plug-in", "[help][look]") {
    juce::ScopedJuceInitialiser_GUI gui;
    HelpDialog dialog(HostKind::Logic);
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&dialog.getLookAndFeel()) != nullptr);
    for (auto* child : dialog.getChildren()) {
        INFO(child->getComponentID().toStdString());
        CHECK(dialog.getLocalBounds().reduced(16).contains(child->getBounds()));
        if (dynamic_cast<juce::TextButton*>(child) != nullptr) {
            CHECK(child->getHeight() == 34);
        }
    }
    auto* text = findById(dialog, "text");
    auto* close = findById(dialog, "close");
    REQUIRE((text != nullptr && close != nullptr));
    CHECK_FALSE(text->getBounds().intersects(close->getBounds()));
    const auto image = dialog.createComponentSnapshot(dialog.getLocalBounds());
    CHECK(image.getPixelAt(1, 1) == juce::Colour(MidiMaidLookAndFeel::kBackground));
}

namespace {

void writeSnapshot(juce::Component& component, const juce::String& name) {
    const auto folder = juce::SystemStats::getEnvironmentVariable("MM_SNAPSHOT_DIR", {});
    REQUIRE(folder.isNotEmpty());
    const auto image = component.createComponentSnapshot(component.getLocalBounds(), false, 2.0f);
    juce::FileOutputStream stream(juce::File(folder).getChildFile(name));
    REQUIRE(stream.openedOk());
    stream.setPosition(0);
    stream.truncate();
    juce::PNGImageFormat png;
    REQUIRE(png.writeImageToStream(image, stream));
}

} // namespace

// Renders the dialog to a PNG file for a look at the design (not a check): MM_SNAPSHOT_DIR=<folder> run with "[.snapshot]".
TEST_CASE("snapshot of the help", "[.snapshot]") {
    juce::ScopedJuceInitialiser_GUI gui;
    HelpDialog dialog(HostKind::AbletonLive);
    writeSnapshot(dialog, "help.png");
}
