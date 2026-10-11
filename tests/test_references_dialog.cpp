#include "core/MidiFile.h"
#include "plugin/MidiMaidLookAndFeel.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/ProcessorBase.h"
#include "plugin/ReferencesDialog.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace mm::plugin;
using namespace mm::core;

namespace {

bool pumpUntil(const std::function<bool()>& done, int timeoutMs = 20000) {
    const auto start = std::chrono::steady_clock::now();
    while (!done()) {
        if (std::chrono::steady_clock::now() - start > std::chrono::milliseconds(timeoutMs)) {
            return false;
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    }
    return true;
}

juce::Component* findById(juce::Component& parent, const juce::String& id) {
    for (auto* component : parent.getChildren()) {
        if (component->getComponentID() == id) {
            return component;
        }
        if (auto* deeper = findById(*component, id)) {
            return deeper;
        }
    }
    return nullptr;
}

template <typename T> T& part(juce::Component& parent, const juce::String& id) {
    auto* found = dynamic_cast<T*>(findById(parent, id));
    REQUIRE(found != nullptr);
    return *found;
}

std::vector<uint8_t> bassFile(int shift = 0, uint8_t numerator = 4) {
    const int roots[] = {45, 45, 48, 43};
    std::vector<PatternNote> list;
    for (uint32_t bar = 0; bar < 4; ++bar) {
        for (uint32_t i = 0; i < 4; ++i) {
            list.push_back({bar * kTicksPerBar + i * 960, 200, 1, static_cast<uint8_t>(roots[bar % 4] + shift), 100});
        }
    }
    MidiFileOptions options;
    options.timeSigNumerator = numerator;
    return writeMidiFile(PatternView{list.data(), list.size(), 4 * kTicksPerBar}, options);
}

struct TempFolder {
    TempFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mm_refdlg_" + juce::Uuid().toString())) {
        folder.createDirectory();
    }
    ~TempFolder() { folder.deleteRecursively(); }
    juce::File write(const juce::String& name, const std::vector<uint8_t>& bytes) const {
        const auto file = folder.getChildFile(name);
        file.getParentDirectory().createDirectory();
        file.replaceWithData(bytes.data(), bytes.size());
        return file;
    }
    juce::File folder;
};

struct Env {
    TempFolder data;
    ReferenceStore store{juce::File(), embeddedStyles()};
    std::unique_ptr<ReferencesDialog> dialog;
    Env() { dialog = std::make_unique<ReferencesDialog>([this]() -> ReferenceStore& { return store; }, "peak_time", "Peak Time"); }
    ~Env() { store.waitForIdle(); }
    juce::ListBox& list() { return part<juce::ListBox>(*dialog, "list"); }
    juce::Component& row(int index) {
        auto* component = list().getComponentForRowNumber(index);
        REQUIRE(component != nullptr);
        return *component;
    }
    juce::String text(const juce::String& id) { return part<juce::Label>(*dialog, id).getText(); }
    void importAndWait(std::vector<juce::File> paths) {
        dialog->filesDropped([&] {
            juce::StringArray names;
            for (const auto& file : paths) {
                names.add(file.getFullPathName());
            }
            return names;
        }(), 0, 0);
        REQUIRE(pumpUntil([&] { return !store.progress().running; }));
        store.waitForIdle();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    }
};

} // namespace

TEST_CASE("the view of an empty set says how to fill it", "[references-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    CHECK(env.text("summary") == "0 Einträge, 0 aktiv");
    CHECK(part<juce::Label>(*env.dialog, "empty").isVisible());
    CHECK(env.text("status").isEmpty());
    CHECK_FALSE(part<juce::TextButton>(*env.dialog, "cancel").isVisible());
    CHECK(part<juce::TextButton>(*env.dialog, "addFiles").isEnabled());
    CHECK(env.list().getListBoxModel()->getNumRows() == 0);
}

TEST_CASE("files and folders dropped on the view are imported and shown", "[references-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto a = env.data.write("a.mid", bassFile());
    env.data.write("sub/b.mid", bassFile(2));
    env.data.write("sub/c.mid", bassFile(0, 3)); // 3/4: skipped
    juce::StringArray good{a.getFullPathName()};
    CHECK(env.dialog->isInterestedInFileDrag(good));
    CHECK(env.dialog->isInterestedInFileDrag(juce::StringArray{env.data.folder.getFullPathName()}));
    const auto text = env.data.write("x.txt", {'h', 'i'});
    CHECK_FALSE(env.dialog->isInterestedInFileDrag(juce::StringArray{text.getFullPathName()}));

    env.importAndWait({a, env.data.folder.getChildFile("sub")});
    CHECK(env.list().getListBoxModel()->getNumRows() == 2);
    CHECK(env.text("summary").startsWith("2 Einträge, 2 aktiv"));
    CHECK(env.text("summary").contains("Bass 2"));
    CHECK(env.text("status") == "2 neu, 1 nicht 4/4");
    CHECK_FALSE(part<juce::Label>(*env.dialog, "empty").isVisible());
    CHECK_FALSE(part<juce::TextButton>(*env.dialog, "cancel").isVisible());

    // the first row: name, role, weight and switch
    auto& row = env.row(0);
    CHECK(part<juce::ComboBox>(row, "role").getText() == "Bass");
    CHECK(part<juce::ComboBox>(row, "weight").getText() == "Gewicht 3");
    CHECK(part<juce::ToggleButton>(row, "active").getToggleState());
    bool seenName = false;
    for (auto* child : row.getChildren()) {
        if (auto* label = dynamic_cast<juce::Label*>(child)) {
            seenName = seenName || label->getText() == "a.mid";
            if (label->getText().contains("Takte")) {
                CHECK(label->getText().contains("4 Takte"));
                CHECK(label->getText().contains("16 Noten"));
                CHECK(label->getText().contains("Moll")); // the scale, in German
            }
        }
    }
    CHECK(seenName);

    // a drop that holds nothing usable imports nothing
    env.dialog->filesDropped(juce::StringArray{text.getFullPathName()}, 0, 0);
    CHECK_FALSE(env.store.progress().running);
    CHECK(env.store.get("peak_time").items.size() == 2);
}

TEST_CASE("role, weight, switch and remove of a row go to the store", "[references-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    env.importAndWait({env.data.write("a.mid", bassFile())});
    const auto id = env.store.get("peak_time").items[0].id;
    auto& row = env.row(0);

    part<juce::ComboBox>(row, "role").setSelectedId(4, juce::sendNotificationSync); // the fourth role: drums
    CHECK(env.store.get("peak_time").items[0].role == TrackRole::Drums);
    part<juce::ComboBox>(row, "weight").setSelectedId(5, juce::sendNotificationSync);
    CHECK(env.store.get("peak_time").items[0].weight == 5);
    auto& active = part<juce::ToggleButton>(row, "active");
    active.setToggleState(false, juce::sendNotificationSync);
    CHECK_FALSE(env.store.get("peak_time").items[0].active);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(env.text("summary").startsWith("1 Einträge, 0 aktiv"));
    CHECK(env.text("summary").contains("Schlagzeug 1"));
    // the row follows the store
    auto& again = env.row(0);
    CHECK(part<juce::ComboBox>(again, "role").getText() == "Schlagzeug");
    CHECK(part<juce::ComboBox>(again, "weight").getText() == "Gewicht 5");
    CHECK_FALSE(part<juce::ToggleButton>(again, "active").getToggleState());
    CHECK(env.store.get("peak_time").items[0].id == id);

    part<juce::TextButton>(env.row(0), "remove").onClick();
    CHECK(env.store.get("peak_time").items.empty());
    REQUIRE(pumpUntil([&] { return env.list().getListBoxModel()->getNumRows() == 0; }));
    CHECK(part<juce::Label>(*env.dialog, "empty").isVisible());
}

TEST_CASE("the status line tells what was not taken", "[references-dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    const auto a = env.data.write("a.mid", bassFile());
    env.importAndWait({a});
    CHECK(env.text("status") == "1 neu");
    env.importAndWait({a, env.data.write("broken.mid", {'n', 'o'})});
    CHECK(env.text("status") == "0 neu, 1 schon vorhanden, 1 nicht lesbar");
}

TEST_CASE("the editor of the hub has the button of the view", "[references-dialog][editor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    InstrumentProcessor processor("Test");
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    auto* button = dynamic_cast<juce::TextButton*>(findById(*editor, "references"));
    REQUIRE(button != nullptr);
    CHECK(button->getButtonText() == "Referenzen");
    CHECK(button->isVisible());
    CHECK(button->isEnabled());
    CHECK_FALSE(button->getBounds().intersects(dynamic_cast<juce::Component*>(findById(*editor, "settings"))->getBounds()));
}

TEST_CASE("the dialog has the look of the plug-in", "[references-dialog][look]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    auto& dialog = *env.dialog;
    auto& close = *findById(dialog, "close");
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&dialog.getLookAndFeel()) != nullptr);
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&close.getLookAndFeel()) != nullptr); // the children inherit it
    const auto image = dialog.createComponentSnapshot(dialog.getLocalBounds());
    CHECK(image.getPixelAt(1, 1) == juce::Colour(MidiMaidLookAndFeel::kBackground));
    CHECK(image.getPixelAt(dialog.getWidth() - 2, dialog.getHeight() - 2) == juce::Colour(MidiMaidLookAndFeel::kBackground));
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
TEST_CASE("snapshot of the references", "[.snapshot]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Env env;
    TempFolder files;
    env.importAndWait({files.write("a.mid", bassFile()), files.write("b.mid", bassFile(2))});
    writeSnapshot(*env.dialog, "references.png");
}
