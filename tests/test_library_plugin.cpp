#include "NoUserSettings.h"
#include "core/MidiFile.h"
#include "core/MidiImport.h"
#include "core/PatternGenerator.h"
#include "core/LibraryIndex.h"
#include "plugin/MidiMaidLookAndFeel.h"
#include "plugin/AtomicFile.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/LibraryDialog.h"
#include "plugin/LibraryStore.h"
#include "plugin/ProcessorBase.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <functional>

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

Pattern made(uint64_t seed, uint32_t bars = 4) {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    const auto result = generatePattern(*embeddedStyles().find("peak_time"), request);
    REQUIRE(result.success);
    Pattern pattern = result.pattern;
    pattern.info.prompt = "p" + std::to_string(seed);
    return pattern;
}

struct TempFolder {
    TempFolder()
        : folder(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mm_lib_" + juce::Uuid().toString())) {}
    ~TempFolder() { folder.deleteRecursively(); }
    juce::File folder;
};

/// The store that `libraryStore()` returns during a test, in memory.
struct ScopedStore {
    ScopedStore() : previous(&libraryStore()) { overrideLibraryStore(&store); }
    ~ScopedStore() { overrideLibraryStore(previous); }
    LibraryStore store{juce::File()};
    LibraryStore* previous;
};

bool slotHas(ProcessorBase& processor, size_t index) {
    return !processor.slotsSnapshot().isEmpty(index);
}

void fillSlot(ProcessorBase& processor, size_t index, uint64_t seed, const std::string& name = {}, uint8_t color = 0) {
    processor.editSlots([&](SlotBank& bank) {
        REQUIRE(bank.setResult(index, made(seed)));
        bank.setName(index, name);
        bank.setColor(index, color);
    });
}

std::vector<uint8_t> clipBytes(uint8_t numerator = 4) {
    std::vector<PatternNote> list;
    for (uint32_t bar = 0; bar < 2; ++bar) {
        for (uint32_t i = 0; i < 4; ++i) {
            list.push_back({bar * kTicksPerBar + i * 960, 200, 1, static_cast<uint8_t>(45 + i * 3), 100});
        }
    }
    MidiFileOptions options;
    options.timeSigNumerator = numerator;
    return writeMidiFile(PatternView{list.data(), list.size(), 2 * kTicksPerBar}, options);
}

juce::File writeFile(const TempFolder& folder, const juce::String& name, const std::vector<uint8_t>& bytes) {
    folder.folder.createDirectory();
    const auto file = folder.folder.getChildFile(name);
    file.replaceWithData(bytes.data(), bytes.size());
    return file;
}

} // namespace

// ---- the file helper -------------------------------------------------------------------------------------------------

TEST_CASE("a file is written whole or not at all, and leaves no temporary file", "[library-plugin][atomic]") {
    TempFolder folder;
    const auto target = folder.folder.getChildFile("sub/a.json");
    REQUIRE(writeFileAtomically(target, "first"));
    CHECK(target.loadFileAsString() == "first");
    REQUIRE(writeFileAtomically(target, std::string(100000, 'x')));
    CHECK(target.getSize() == 100000);
    CHECK(target.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*").size() == 1);
    // a target that cannot be replaced (a folder of that name) fails and leaves the folder as it was
    const auto blocked = folder.folder.getChildFile("sub/dir.json");
    blocked.createDirectory();
    CHECK_FALSE(writeFileAtomically(blocked, "x"));
    CHECK(blocked.isDirectory());
    CHECK(target.getParentDirectory().findChildFiles(juce::File::findFiles, false, "*").size() == 1);
    // a folder that cannot be made
    CHECK_FALSE(writeFileAtomically(target.getChildFile("inside/b.json"), "x"));
}

TEST_CASE("stale temporary files of a crashed write are removed", "[library-plugin][atomic]") {
    TempFolder folder;
    folder.folder.createDirectory();
    folder.folder.getChildFile(".a.json.1234.mmtmp").replaceWithText("half");
    folder.folder.getChildFile(".b.json.beef.mmtmp").replaceWithText("half");
    folder.folder.getChildFile("keep.json").replaceWithText("{}");
    folder.folder.getChildFile("other.tmp").replaceWithText("x");
    CHECK(removeStaleTemporaries(folder.folder) == 2);
    CHECK(folder.folder.findChildFiles(juce::File::findFiles, false, "*").size() == 2);
    CHECK(removeStaleTemporaries(folder.folder) == 0);
}

// ---- the store -------------------------------------------------------------------------------------------------------

TEST_CASE("a saved pattern shows in the list at once and loads back", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore store{juce::File()};
    REQUIRE(store.ready());
    CHECK(store.list().empty());
    const auto pattern = made(3);
    const auto saved = store.savePattern("My bass", pattern, "Intro", 4);
    REQUIRE(saved.error == LibraryStore::SaveError::None);
    REQUIRE(isValidLibraryId(saved.id));
    const auto list = store.list();
    REQUIRE(list.size() == 1);
    CHECK(list[0].name == "My bass");
    CHECK(list[0].id == saved.id);
    CHECK(store.info(saved.id) == list[0]);
    CHECK_FALSE(store.info("nope").has_value());

    std::optional<LibraryLoadResult> loaded;
    store.load(saved.id, [&](LibraryLoadResult result) { loaded = std::move(result); });
    REQUIRE(pumpUntil([&] { return loaded.has_value(); }));
    REQUIRE(loaded->ok());
    CHECK(loaded->entry->pattern == pattern);
    CHECK(loaded->entry->slotName == "Intro");
    CHECK(loaded->entry->slotColor == 4);

    std::optional<LibraryLoadResult> missing;
    store.load("nope", [&](LibraryLoadResult result) { missing = std::move(result); });
    REQUIRE(pumpUntil([&] { return missing.has_value(); }));
    CHECK_FALSE(missing->ok());
}

TEST_CASE("the list is newest first and the ids never clash", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore store{juce::File()};
    std::set<std::string> ids;
    for (int i = 0; i < 40; ++i) {
        const auto result = store.savePattern("n" + std::to_string(i), made(1), "", 0);
        REQUIRE(result.error == LibraryStore::SaveError::None);
        ids.insert(result.id);
    }
    CHECK(ids.size() == 40);
    const auto list = store.list();
    REQUIRE(list.size() == 40);
    for (size_t i = 1; i < list.size(); ++i) {
        CHECK(list[i - 1].createdUnixMs >= list[i].createdUnixMs);
    }
}

TEST_CASE("remove and rename change the list and the file", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    LibraryStore store{folder.folder};
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    const auto a = store.savePattern("a", made(1), "", 0);
    const auto b = store.savePattern("b", made(2), "", 0);
    store.waitForIdle();
    CHECK(folder.folder.getChildFile(juce::String(a.id) + ".json").existsAsFile());
    CHECK(store.rename(a.id, "  renamed "));
    CHECK(store.info(a.id)->name == "renamed");
    CHECK_FALSE(store.rename(a.id, "   ")); // an empty name changes nothing
    CHECK(store.info(a.id)->name == "renamed");
    CHECK_FALSE(store.rename("nope", "x"));
    store.waitForIdle();
    LibraryStore again{folder.folder};
    REQUIRE(pumpUntil([&] { return again.ready(); }));
    CHECK(again.info(a.id)->name == "renamed");
    CHECK(again.list().size() == 2);

    CHECK(store.remove(b.id));
    CHECK_FALSE(store.remove(b.id));
    CHECK_FALSE(store.remove("nope"));
    store.waitForIdle();
    CHECK_FALSE(folder.folder.getChildFile(juce::String(b.id) + ".json").existsAsFile());
    CHECK(store.list().size() == 1);
}

TEST_CASE("the entries are read again from the folder, bad files are skipped and counted", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    std::string id;
    {
        LibraryStore store{folder.folder};
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        id = store.savePattern("kept", made(5), "", 0).id;
        store.waitForIdle();
    }
    folder.folder.getChildFile("broken.json").replaceWithText("{ not json");
    folder.folder.getChildFile("Upper.json").replaceWithText("{}");           // not a valid id: not ours
    folder.folder.getChildFile("notes.txt").replaceWithText("hello");
    folder.folder.getChildFile(".left.json.12.mmtmp").replaceWithText("half"); // a crashed write
    LibraryStore store{folder.folder};
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    REQUIRE(store.list().size() == 1);
    CHECK(store.list()[0].id == id);
    CHECK(store.unreadableCount() == 1);
    CHECK_FALSE(folder.folder.getChildFile(".left.json.12.mmtmp").existsAsFile());
    CHECK(folder.folder.getChildFile("broken.json").existsAsFile()); // never deleted: it may be repairable
}

TEST_CASE("a set is saved and loaded, an empty bank is no set", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore store{juce::File()};
    CHECK(store.saveSet("none", SlotBank{}).error == LibraryStore::SaveError::NothingToSave);
    CHECK(store.list().empty());
    SlotBank bank;
    REQUIRE(bank.setResult(2, made(8)));
    REQUIRE(bank.setResult(9, made(9)));
    const auto saved = store.saveSet("two", bank);
    REQUIRE(saved.error == LibraryStore::SaveError::None);
    CHECK(store.info(saved.id)->kind == LibraryKind::Set);
    CHECK(store.info(saved.id)->slotCount == 2);
}

TEST_CASE("the library holds at most the limit and says so", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore store{juce::File(), 3};
    for (int i = 0; i < 3; ++i) {
        CHECK(store.savePattern("x", made(1), "", 0).error == LibraryStore::SaveError::None);
    }
    const auto full = store.savePattern("x", made(1), "", 0);
    CHECK(full.error == LibraryStore::SaveError::Full);
    CHECK(full.id.empty());
    CHECK(store.list().size() == 3);
    CHECK(store.saveSet("s", [] { SlotBank b; b.setResult(0, made(1)); return b; }()).error ==
          LibraryStore::SaveError::Full);
}

TEST_CASE("the store tells the window when the list changes", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore store{juce::File()};
    int changes = 0;
    store.setOnChanged([&] { ++changes; });
    const auto id = store.savePattern("x", made(1), "", 0).id;
    REQUIRE(pumpUntil([&] { return changes >= 1; }));
    const int afterSave = changes;
    store.rename(id, "y");
    REQUIRE(pumpUntil([&] { return changes > afterSave; }));
    const int afterRename = changes;
    store.remove(id);
    REQUIRE(pumpUntil([&] { return changes > afterRename; }));
}

// ---- the processor ---------------------------------------------------------------------------------------------------

TEST_CASE("saving the slot makes an entry, an empty slot saves nothing", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    CHECK_FALSE(processor.saveToLibrary("x"));
    CHECK(processor.generationStatus() == GenerationStatus::LibraryNothingToSave);
    CHECK(scoped.store.list().empty());

    fillSlot(processor, 0, 4, "Intro", 2);
    CHECK(processor.saveToLibrary("Opening"));
    CHECK(processor.generationStatus() == GenerationStatus::LibrarySaved);
    REQUIRE(scoped.store.list().size() == 1);
    CHECK(scoped.store.list()[0].name == "Opening");
    // another slot by choice, and an empty name falls back to the style
    fillSlot(processor, 5, 6);
    CHECK(processor.saveToLibrary("", 5));
    CHECK(scoped.store.list().size() == 2);
    CHECK(processor.saveToLibrary("", 7) == false); // slot 8 is empty
    CHECK(processor.generationStatus() == GenerationStatus::LibraryNothingToSave);
}

TEST_CASE("a voice instance leaves the library to the hub", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    fillSlot(processor, 0, 4);
    auto settings = processor.instanceSettings();
    settings.role = InstanceRole::Voice;
    processor.setInstanceSettings(settings);
    CHECK_FALSE(processor.saveToLibrary("x"));
    CHECK(processor.generationStatus() == GenerationStatus::UseHub);
    CHECK_FALSE(processor.saveSetToLibrary("x"));
    CHECK_FALSE(processor.loadFromLibrary("x"));
    CHECK_FALSE(processor.importMidiToLibrary(juce::File("/nowhere/x.mid")));
    CHECK(processor.generationStatus() == GenerationStatus::UseHub);
    CHECK(scoped.store.list().empty());
}

TEST_CASE("loading an entry puts it into the slot as a result, with name, colour and undo", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    const auto saved = scoped.store.savePattern("lib", made(11), "Drop", 3);
    fillSlot(processor, 0, 4, "old", 1);
    const auto before = *processor.slotsSnapshot().slot(0)->pattern;

    REQUIRE(processor.loadFromLibrary(saved.id));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::LibraryLoaded; }));
    auto bank = processor.slotsSnapshot();
    CHECK(bank.slot(0)->pattern->voices[1].notes == made(11).voices[1].notes);
    CHECK(bank.slot(0)->name == "Drop");
    CHECK(bank.slot(0)->color == 3);
    REQUIRE(bank.slot(0)->history.size() >= 2); // the old pattern stays reachable
    CHECK(bank.slot(0)->history.front() == before);
    CHECK(processor.canUndo());
    processor.undo();
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices == before.voices); // the version is a new one

    // into another slot, by choice
    REQUIRE(processor.loadFromLibrary(saved.id, 6));
    REQUIRE(pumpUntil([&] { return slotHas(processor, 6); }));
    CHECK(processor.slotsSnapshot().slot(6)->pattern->voices[1].notes == made(11).voices[1].notes);
}

TEST_CASE("an unknown or damaged entry loads nothing and says so", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    fillSlot(processor, 0, 4);
    CHECK_FALSE(processor.loadFromLibrary("nope"));
    CHECK(processor.generationStatus() == GenerationStatus::LibraryLoadFailed);
    CHECK(processor.slotsSnapshot().slot(0)->history.size() == 1);

    // an entry whose file went bad after the list was read
    TempFolder folder;
    LibraryStore store{folder.folder};
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    overrideLibraryStore(&store);
    const auto id = store.savePattern("x", made(1), "", 0).id;
    store.waitForIdle();
    folder.folder.getChildFile(juce::String(id) + ".json").replaceWithText("garbage");
    CHECK(processor.loadFromLibrary(id));
    CHECK(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::LibraryLoadFailed; }));
    overrideLibraryStore(&scoped.store);
    CHECK(processor.slotsSnapshot().slot(0)->history.size() == 1);
}

TEST_CASE("a set fills the slots it holds and leaves the others", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor source("Source");
    fillSlot(source, 1, 20, "A", 1);
    fillSlot(source, 3, 21, "B", 2);
    REQUIRE(source.saveSetToLibrary("both"));
    CHECK(source.generationStatus() == GenerationStatus::LibrarySetSaved);
    const auto id = scoped.store.list()[0].id;

    InstrumentProcessor target("Target");
    fillSlot(target, 0, 30, "keep", 5);
    fillSlot(target, 1, 31, "replaced", 6);
    REQUIRE(target.loadFromLibrary(id));
    REQUIRE(pumpUntil([&] { return target.generationStatus() == GenerationStatus::LibrarySetLoaded; }));
    const auto bank = target.slotsSnapshot();
    CHECK(bank.slot(0)->name == "keep"); // not in the set
    CHECK(bank.slot(1)->name == "A");
    CHECK(bank.slot(1)->history.size() == 2); // the replaced pattern is in the history
    CHECK(bank.slot(3)->name == "B");
    CHECK(bank.slot(3)->color == 2);
    CHECK_FALSE(bank.slot(2)->pattern.has_value());

    InstrumentProcessor empty("Empty");
    CHECK_FALSE(empty.saveSetToLibrary("nothing"));
    CHECK(empty.generationStatus() == GenerationStatus::LibraryNothingToSave);
}

TEST_CASE("a full library is reported when saving", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore small{juce::File(), 1};
    LibraryStore* previous = &libraryStore();
    overrideLibraryStore(&small);
    InstrumentProcessor processor("Test");
    fillSlot(processor, 0, 4);
    CHECK(processor.saveToLibrary("a"));
    CHECK_FALSE(processor.saveToLibrary("b"));
    CHECK(processor.generationStatus() == GenerationStatus::LibraryFull);
    CHECK_FALSE(processor.saveSetToLibrary("c"));
    CHECK(processor.generationStatus() == GenerationStatus::LibraryFull);
    overrideLibraryStore(previous);
}

TEST_CASE("a pattern is exported as a MIDI file, a set is not", "[library-plugin][processor][export]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    const auto pattern = made(12, 2);
    const auto id = scoped.store.savePattern("p", pattern, "", 0).id;
    TempFolder folder;
    folder.folder.createDirectory();
    const auto target = folder.folder.getChildFile("out.mid");
    REQUIRE(processor.exportLibraryEntry(id, target));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::LibraryExported; }));
    juce::MemoryBlock bytes;
    REQUIRE(target.loadFileAsData(bytes));
    const auto read = readMidiFile(static_cast<const uint8_t*>(bytes.getData()), bytes.getSize());
    REQUIRE(read.ok());
    CHECK(read.clip.lengthTicks == 2 * kTicksPerBar);
    CHECK_FALSE(read.clip.notes.empty());

    SlotBank bank;
    bank.setResult(0, made(13));
    const auto setId = scoped.store.saveSet("s", bank).id;
    CHECK_FALSE(processor.exportLibraryEntry(setId, folder.folder.getChildFile("set.mid")));
    CHECK(processor.generationStatus() == GenerationStatus::LibraryExportFailed);
    CHECK_FALSE(folder.folder.getChildFile("set.mid").existsAsFile());
    CHECK_FALSE(processor.exportLibraryEntry("nope", folder.folder.getChildFile("x.mid")));

    // a target that cannot be written: a folder of that name
    const auto blocked = folder.folder.getChildFile("blocked.mid");
    blocked.createDirectory();
    REQUIRE(processor.exportLibraryEntry(id, blocked));
    CHECK(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::LibraryExportFailed; }));
}

TEST_CASE("a MIDI file becomes an entry with the file as a locked voice", "[library-plugin][processor][import]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    TempFolder folder;
    const auto file = writeFile(folder, "My Line.mid", clipBytes());
    REQUIRE(processor.importMidiToLibrary(file));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::LibraryImported; }));
    REQUIRE(scoped.store.list().size() == 1);
    const auto info = scoped.store.list()[0];
    CHECK(info.name == "My Line");
    CHECK(info.lengthBars == 2);
    std::optional<LibraryLoadResult> loaded;
    scoped.store.load(info.id, [&](LibraryLoadResult r) { loaded = std::move(r); });
    REQUIRE(pumpUntil([&] { return loaded.has_value(); }));
    REQUIRE(loaded->ok());
    CHECK(isVoiceLocked(loaded->entry->pattern->voices[0]));
    CHECK(loaded->entry->pattern->voices[0].notes.size() == 8);
    // the slots of the instance were not touched
    CHECK_FALSE(slotHas(processor, 0));

    // not 4/4, not a MIDI file, not even named like one
    const auto waltz = writeFile(folder, "waltz.mid", clipBytes(3));
    REQUIRE(processor.importMidiToLibrary(waltz));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::ImportNotFourFour; }));
    const auto broken = writeFile(folder, "broken.mid", {1, 2, 3});
    REQUIRE(processor.importMidiToLibrary(broken));
    REQUIRE(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::ImportUnreadable; }));
    CHECK_FALSE(processor.importMidiToLibrary(writeFile(folder, "text.txt", {1})));
    CHECK(scoped.store.list().size() == 1);
}

// ---- the window ------------------------------------------------------------------------------------------------------

namespace {

juce::Component* find(juce::Component& parent, const juce::String& id) {
    for (auto* child : parent.getChildren()) {
        if (child->getComponentID() == id) {
            return child;
        }
        if (auto* deeper = find(*child, id)) {
            return deeper;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the window saves, loads, renames and removes with the buttons", "[library-plugin][dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    fillSlot(processor, 0, 4);
    LibraryDialog dialog(processor, processor.libraryAliveFlag(), [&]() -> LibraryStore& { return scoped.store; });
    auto& name = *dynamic_cast<juce::TextEditor*>(find(dialog, "name"));
    auto& list = *dynamic_cast<juce::ListBox*>(find(dialog, "list"));
    auto& load = *dynamic_cast<juce::TextButton*>(find(dialog, "load"));
    auto& remove = *dynamic_cast<juce::TextButton*>(find(dialog, "remove"));
    auto& rename = *dynamic_cast<juce::TextButton*>(find(dialog, "rename"));
    auto& exportButton = *dynamic_cast<juce::TextButton*>(find(dialog, "export"));
    auto& status = *dynamic_cast<juce::Label*>(find(dialog, "status"));
    CHECK(find(dialog, "empty")->isVisible());
    CHECK_FALSE(load.isEnabled()); // nothing selected
    CHECK_FALSE(remove.isEnabled());
    CHECK_FALSE(rename.isEnabled());
    CHECK_FALSE(exportButton.isEnabled());

    name.setText("First", juce::dontSendNotification);
    dynamic_cast<juce::TextButton*>(find(dialog, "saveSlot"))->onClick();
    REQUIRE(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 1; }));
    CHECK(name.getText().isEmpty());
    CHECK_FALSE(find(dialog, "empty")->isVisible());
    CHECK(pumpUntil([&] { return status.getText().contains("gespeichert"); }));

    list.selectRow(0);
    CHECK(load.isEnabled());
    CHECK(exportButton.isEnabled());
    // a set cannot be exported as a file
    name.setText("Whole", juce::dontSendNotification);
    dynamic_cast<juce::TextButton*>(find(dialog, "saveSet"))->onClick();
    REQUIRE(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 2; }));
    list.selectRow(0); // the newest first: the set
    CHECK(scoped.store.list()[0].kind == LibraryKind::Set);
    CHECK_FALSE(exportButton.isEnabled());
    CHECK(remove.isEnabled());

    // rename the selected entry
    name.setText("Renamed", juce::dontSendNotification);
    rename.onClick();
    CHECK(scoped.store.list()[0].name == "Renamed");
    CHECK(name.getText().isEmpty());

    // the first click on remove asks, a change of the selection withdraws the question, the second click removes
    const auto before = remove.getButtonText();
    remove.onClick();
    CHECK(remove.getButtonText() != before);
    CHECK(scoped.store.list().size() == 2);
    list.selectRow(1);
    CHECK(remove.getButtonText() == before);
    remove.onClick();
    remove.onClick();
    REQUIRE(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 1; }));
    CHECK(scoped.store.list().size() == 1);

    // load what is left: the set
    list.selectRow(0);
    load.onClick();
    CHECK(pumpUntil([&] { return processor.generationStatus() == GenerationStatus::LibrarySetLoaded; }));
}

TEST_CASE("the window does nothing once its instance is gone", "[library-plugin][dialog]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    std::shared_ptr<bool> flag;
    std::unique_ptr<LibraryDialog> dialog;
    {
        InstrumentProcessor processor("Test");
        fillSlot(processor, 0, 4);
        scoped.store.savePattern("x", made(1), "", 0);
        flag = processor.libraryAliveFlag();
        dialog = std::make_unique<LibraryDialog>(processor, flag, [&]() -> LibraryStore& { return scoped.store; });
        CHECK(*flag);
    }
    CHECK_FALSE(*flag);
    auto& list = *dynamic_cast<juce::ListBox*>(find(*dialog, "list"));
    list.selectRow(0);
    dynamic_cast<juce::TextButton*>(find(*dialog, "load"))->onClick(); // would touch the processor
    dynamic_cast<juce::TextButton*>(find(*dialog, "saveSlot"))->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    CHECK(scoped.store.list().size() == 1);
    dialog.reset();
}

TEST_CASE("every library status has its text", "[library-plugin]") {
    for (const auto status :
         {GenerationStatus::LibrarySaved, GenerationStatus::LibrarySetSaved, GenerationStatus::LibraryNothingToSave,
          GenerationStatus::LibraryFull, GenerationStatus::LibraryLoaded, GenerationStatus::LibrarySetLoaded,
          GenerationStatus::LibraryLoadFailed, GenerationStatus::LibraryImported, GenerationStatus::LibraryExported,
          GenerationStatus::LibraryExportFailed}) {
        CHECK_FALSE(libraryStatusKey(status).empty());
    }
    CHECK(libraryStatusKey(GenerationStatus::Done).empty());
    CHECK(libraryStatusKey(GenerationStatus::ImportApplied).empty());
}

TEST_CASE("an entry of the memory store keeps its new name when it is loaded", "[library-plugin][store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    LibraryStore store{juce::File()};
    const auto id = store.savePattern("old", made(2), "", 0).id;
    REQUIRE(store.rename(id, "new"));
    std::optional<LibraryLoadResult> loaded;
    store.load(id, [&](LibraryLoadResult r) { loaded = std::move(r); });
    REQUIRE(pumpUntil([&] { return loaded.has_value(); }));
    CHECK(loaded->entry->info.name == "new");
}

TEST_CASE("the store says why an export did not happen", "[library-plugin][store][export]") {
    juce::ScopedJuceInitialiser_GUI gui;
    using Result = LibraryStore::ExportResult;
    const auto style = *embeddedStyles().find("peak_time");
    TempFolder folder;
    folder.folder.createDirectory();
    const auto run = [&](LibraryStore& store, const std::string& id, const juce::File& target) {
        std::optional<Result> result;
        store.exportMidi(id, style, 120.0, target, [&](Result r) { result = r; });
        REQUIRE(pumpUntil([&] { return result.has_value(); }));
        return *result;
    };
    // in memory and from a folder: the same answers
    for (const bool withFolder : {false, true}) {
        TempFolder data;
        LibraryStore store{withFolder ? data.folder : juce::File()};
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        const auto pattern = store.savePattern("p", made(3, 2), "", 0).id;
        SlotBank bank;
        bank.setResult(0, made(4));
        const auto set = store.saveSet("s", bank).id;
        Pattern silent = made(5);
        for (auto& voice : silent.voices) {
            voice.notes.clear();
        }
        const auto empty = store.savePattern("e", silent, "", 0).id;
        store.waitForIdle();
        const auto target = folder.folder.getChildFile(withFolder ? "a.mid" : "b.mid");
        CHECK(run(store, pattern, target) == Result::Done);
        CHECK(target.existsAsFile());
        CHECK(run(store, "nope", folder.folder.getChildFile("c.mid")) == Result::NoSuchEntry);
        CHECK(run(store, set, folder.folder.getChildFile("d.mid")) == Result::NotAPattern);
        CHECK(run(store, empty, folder.folder.getChildFile("e.mid")) == Result::NoNotes);
        const auto blocked = folder.folder.getChildFile(withFolder ? "f.mid" : "g.mid");
        blocked.createDirectory();
        CHECK(run(store, pattern, blocked) == Result::WriteFailed);
        CHECK_FALSE(folder.folder.getChildFile("d.mid").existsAsFile());
        CHECK_FALSE(folder.folder.getChildFile("e.mid").existsAsFile());
    }
}

TEST_CASE("an instance that goes away while an entry loads is not called afterwards", "[library-plugin][processor]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    const auto id = scoped.store.savePattern("x", made(2), "", 0).id;
    {
        InstrumentProcessor processor("Test");
        REQUIRE(processor.loadFromLibrary(id));
    } // the answer comes after this point
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    SUCCEED();
}

TEST_CASE("an entry whose file went bad cannot be exported", "[library-plugin][store][export]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    LibraryStore store{data.folder};
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    const auto id = store.savePattern("p", made(3, 2), "", 0).id;
    store.waitForIdle();
    data.folder.getChildFile(juce::String(id) + ".json").replaceWithText("garbage");
    std::optional<LibraryStore::ExportResult> result;
    store.exportMidi(id, *embeddedStyles().find("peak_time"), 120.0, data.folder.getChildFile("x.mid"),
                     [&](LibraryStore::ExportResult r) { result = r; });
    REQUIRE(pumpUntil([&] { return result.has_value(); }));
    CHECK(*result == LibraryStore::ExportResult::NoSuchEntry);
    CHECK_FALSE(data.folder.getChildFile("x.mid").existsAsFile());
}

// ---- the index and crashes ---------------------------------------------------------------------------------------------

namespace {

juce::File indexFileOf(const TempFolder& folder) {
    return folder.folder.getChildFile(LibraryStore::kIndexFileName);
}

std::vector<std::string> ids(const LibraryStore& store) {
    std::vector<std::string> result;
    for (const auto& info : store.list()) {
        result.push_back(info.id);
    }
    std::sort(result.begin(), result.end());
    return result;
}

/// A store on the folder, read completely.
std::unique_ptr<LibraryStore> open(const TempFolder& folder) {
    auto store = std::make_unique<LibraryStore>(folder.folder);
    REQUIRE(pumpUntil([&] { return store->ready(); }));
    store->waitForIdle();
    return store;
}

std::vector<std::string> fill(const TempFolder& folder, int count) {
    auto store = open(folder);
    for (int i = 0; i < count; ++i) {
        REQUIRE(store->savePattern("entry " + std::to_string(i), made(static_cast<uint64_t>(i + 1)), "", 0).error ==
                LibraryStore::SaveError::None);
    }
    store->waitForIdle();
    return ids(*store);
}

} // namespace

TEST_CASE("the index follows the list and holds what the files are", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 12);
    REQUIRE(indexFileOf(folder).existsAsFile());
    const auto loaded = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString());
    REQUIRE(loaded.ok);
    REQUIRE(loaded.records.size() == 12);
    for (const auto& record : loaded.records) {
        const auto file = folder.folder.getChildFile(juce::String(record.info.id) + ".json");
        CHECK(record.fileSize == file.getSize());
        CHECK(record.modifiedMs == file.getLastModificationTime().toMilliseconds());
    }
    std::vector<std::string> inIndex;
    for (const auto& record : loaded.records) {
        inIndex.push_back(record.info.id);
    }
    CHECK(inIndex == saved);

    // remove and rename change the index, too
    auto store = open(folder);
    store->remove(saved[0]);
    store->rename(saved[1], "renamed");
    store->waitForIdle();
    const auto after = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString());
    REQUIRE(after.ok);
    CHECK(after.records.size() == 11);
    const auto renamed = std::find_if(after.records.begin(), after.records.end(),
                                      [&](const auto& r) { return r.info.id == saved[1]; });
    REQUIRE(renamed != after.records.end());
    CHECK(renamed->info.name == "renamed");
    CHECK(renamed->fileSize == folder.folder.getChildFile(juce::String(saved[1]) + ".json").getSize());
}

TEST_CASE("a burst of saves ends with one index that holds all of them", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 40);
    const auto loaded = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString());
    REQUIRE(loaded.ok);
    CHECK(loaded.records.size() == 40);
    auto again = open(folder);
    CHECK(again->indexedCount() == 40);
    CHECK(again->parsedCount() == 0);
    CHECK(ids(*again) == saved);
}

TEST_CASE("with an intact index no entry file is read at the start", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 10);
    auto first = open(folder);
    const auto list = first->list();
    first.reset();
    auto store = open(folder);
    CHECK(store->indexedCount() == 10);
    CHECK(store->parsedCount() == 0);
    CHECK(store->list() == list);
    // and it does not rewrite what is right
    const auto stamp = indexFileOf(folder).getLastModificationTime();
    store->waitForIdle();
    CHECK(indexFileOf(folder).getLastModificationTime() == stamp);
}

TEST_CASE("a file that changed behind the index is read again", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 5);
    // another entry's text under the id of the first: other size and time
    const auto other = libraryEntryToString(makePatternEntry(saved[0], "swapped by hand", made(99, 8), "", 0, 1));
    folder.folder.getChildFile(juce::String(saved[0]) + ".json").replaceWithText(juce::String::fromUTF8(other.c_str()));
    auto store = open(folder);
    CHECK(store->indexedCount() == 4);
    CHECK(store->parsedCount() == 1);
    CHECK(store->info(saved[0])->name == "swapped by hand");
    CHECK(store->info(saved[0])->lengthBars == 8);
    // the index was written again and is right now
    auto next = open(folder);
    CHECK(next->indexedCount() == 5);
    CHECK(next->parsedCount() == 0);
    CHECK(next->info(saved[0])->name == "swapped by hand");
}

TEST_CASE("a file that came and one that went are noticed", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 4);
    folder.folder.getChildFile(juce::String(saved[2]) + ".json").deleteFile();
    const auto added = libraryEntryToString(makePatternEntry("from-elsewhere", "copied in", made(7), "", 0, 5));
    folder.folder.getChildFile("from-elsewhere.json").replaceWithText(juce::String::fromUTF8(added.c_str()));
    auto store = open(folder);
    CHECK(store->indexedCount() == 3);
    CHECK(store->parsedCount() == 1);
    CHECK(store->list().size() == 4);
    CHECK_FALSE(store->info(saved[2]).has_value());
    CHECK(store->info("from-elsewhere")->name == "copied in");
    auto next = open(folder);
    CHECK(next->indexedCount() == 4);
    CHECK(next->parsedCount() == 0);
}

TEST_CASE("without a usable index the library is built from the files, and the index comes back",
          "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 6);
    const auto good = indexFileOf(folder).loadFileAsString();
    const auto list = open(folder)->list();
    for (const char* damage : {"", "garbage", "{\"indexVersion\":7,\"entries\":[]}", "{\"indexVersion\":1}"}) {
        SECTION(std::string("index: ") + damage) {
            if (std::string(damage).empty()) {
                indexFileOf(folder).deleteFile();
            } else {
                indexFileOf(folder).replaceWithText(damage);
            }
            auto store = open(folder);
            CHECK(store->parsedCount() == 6);
            CHECK(store->indexedCount() == 0);
            CHECK(store->list() == list);
            auto next = open(folder);
            CHECK(next->indexedCount() == 6);
            CHECK(next->parsedCount() == 0);
        }
    }
    SECTION("a truncated index") {
        indexFileOf(folder).replaceWithText(good.substring(0, good.length() / 2));
        auto store = open(folder);
        CHECK(store->parsedCount() == 6);
        CHECK(store->list() == list);
    }
}

TEST_CASE("an index that lists a file that is gone, or lacks a file that is there, is corrected", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 5);
    // a crash between the write of an entry and the write of the index: the file is there, the index does not know it
    auto records = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString()).records;
    records.erase(std::remove_if(records.begin(), records.end(), [&](const auto& r) { return r.info.id == saved[3]; }),
                  records.end());
    // ... and an entry the index lists but whose file is gone
    auto ghost = records.front();
    ghost.info.id = "ghost";
    records.push_back(ghost);
    indexFileOf(folder).replaceWithText(juce::String::fromUTF8(libraryIndexToString(records).c_str()));
    auto store = open(folder);
    CHECK(store->list().size() == 5);
    CHECK(store->info(saved[3]).has_value());
    CHECK_FALSE(store->info("ghost").has_value());
    CHECK(store->parsedCount() == 1);
    CHECK(store->indexedCount() == 4);
    auto next = open(folder);
    CHECK(next->parsedCount() == 0);
    CHECK(next->list().size() == 5);
}

TEST_CASE("the index file is never taken for an entry", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    fill(folder, 3);
    auto store = open(folder);
    CHECK(store->unreadableCount() == 0);
    CHECK(store->list().size() == 3);
}

TEST_CASE("a crash in the middle of a write leaves the old entry whole", "[library-plugin][index][atomic]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 3);
    const auto target = folder.folder.getChildFile(juce::String(saved[0]) + ".json");
    const auto before = target.loadFileAsString();
    // what a write that died after its first bytes leaves: a temporary file, and the entry as it was
    folder.folder.getChildFile("." + target.getFileName() + ".9f3a.mmtmp").replaceWithText(before.substring(0, 40));
    auto store = open(folder);
    CHECK(target.loadFileAsString() == before);
    CHECK(store->list().size() == 3);
    CHECK(store->unreadableCount() == 0);
    CHECK(folder.folder.findChildFiles(juce::File::findFiles, false, "*.mmtmp").isEmpty());
    // the old entry still loads
    std::optional<LibraryLoadResult> loaded;
    store->load(saved[0], [&](LibraryLoadResult r) { loaded = std::move(r); });
    REQUIRE(pumpUntil([&] { return loaded.has_value(); }));
    CHECK(loaded->ok());
}

TEST_CASE("an entry file that was cut short is reported and left, the rest loads", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 4);
    const auto cut = folder.folder.getChildFile(juce::String(saved[1]) + ".json");
    const auto text = cut.loadFileAsString();
    cut.replaceWithText(text.substring(0, text.length() / 3));
    auto store = open(folder);
    CHECK(store->list().size() == 3);
    CHECK(store->unreadableCount() == 1);
    CHECK(store->parsedCount() == 1); // the changed file was looked at
    CHECK(cut.existsAsFile());       // never deleted
    // the next start knows the file is bad only by reading it again: it is read, found bad, and still left alone
    auto next = open(folder);
    CHECK(next->list().size() == 3);
    CHECK(next->unreadableCount() == 1);
    CHECK(cut.existsAsFile());
}

TEST_CASE("what was saved just before the store goes is written", "[library-plugin][index][atomic]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    std::vector<std::string> saved;
    {
        LibraryStore store{folder.folder};
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        for (int i = 0; i < 25; ++i) {
            saved.push_back(store.savePattern("e", made(static_cast<uint64_t>(i + 1)), "", 0).id);
        }
        store.remove(saved.back());
        saved.pop_back();
        store.rename(saved[0], "last word");
    } // gone at once, with the writes still queued
    auto store = open(folder);
    std::sort(saved.begin(), saved.end());
    CHECK(ids(*store) == saved);
    CHECK(store->info(saved[0])->name == "last word");
    CHECK(store->parsedCount() == 0); // the index was written, too
}

TEST_CASE("a library of a thousand entries starts from the index without reading the files", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    folder.folder.createDirectory();
    const auto pattern = made(3);
    std::vector<LibraryIndexRecord> records;
    for (int i = 0; i < 1000; ++i) {
        const std::string id = "e" + std::to_string(100000 + i);
        const auto entry = makePatternEntry(id, "entry " + std::to_string(i), pattern, "", 0, 1000 + i);
        const auto file = folder.folder.getChildFile(juce::String(id) + ".json");
        file.replaceWithText(juce::String::fromUTF8(libraryEntryToString(entry).c_str()));
        LibraryIndexRecord record;
        record.info = entry.info;
        record.fileSize = file.getSize();
        record.modifiedMs = file.getLastModificationTime().toMilliseconds();
        records.push_back(record);
    }
    indexFileOf(folder).replaceWithText(juce::String::fromUTF8(libraryIndexToString(records).c_str()));
    const auto start = std::chrono::steady_clock::now();
    auto store = open(folder);
    const auto indexed = std::chrono::steady_clock::now() - start;
    CHECK(store->indexedCount() == 1000);
    CHECK(store->parsedCount() == 0);
    // without the index every file is parsed
    indexFileOf(folder).deleteFile();
    const auto slowStart = std::chrono::steady_clock::now();
    auto rebuilt = open(folder);
    const auto parsed = std::chrono::steady_clock::now() - slowStart;
    CHECK(rebuilt->parsedCount() == 1000);
    CHECK(rebuilt->list() == store->list());
    CHECK(indexed < parsed); // the index is the quicker way
    CHECK(indexed < std::chrono::seconds(2));
}

TEST_CASE("a saved pattern stays as it was when the reference set changes", "[library-plugin][reproducible]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto style = *embeddedStyles().find("peak_time");
    Chord third;
    third.rootOffset = 3;
    third.quality = ChordQuality::Major;
    Chord seventh;
    seventh.rootOffset = 10;
    seventh.quality = ChordQuality::Major;
    const std::vector<WeightedProgression> mine{{{Chord{}, third, seventh}, 100000}};
    const std::vector<WeightedProgression> changedSet{{{Chord{}, seventh, third}, 100000}};
    // a seed whose chords come from the set (the profile also draws a single chord): the changed set then differs
    GenerationRequest request;
    request.lengthBars = 4;
    request.referenceSetId = kReferenceSetId;
    SelectionResult first, changed;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        request.seed = seed;
        request.personalProgressions = mine;
        first = generatePattern(style, request);
        request.personalProgressions = changedSet;
        changed = generatePattern(style, request);
        if (first.success && changed.success && !(changed.pattern == first.pattern)) {
            break;
        }
    }
    REQUIRE(first.success);
    REQUIRE(changed.success);
    CHECK_FALSE(changed.pattern == first.pattern); // generating again would not give the saved pattern
    std::string id;
    {
        LibraryStore store{folder.folder};
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        id = store.savePattern("with my set", first.pattern, "", 0).id;
    }
    auto store = open(folder);
    std::optional<LibraryLoadResult> loaded;
    store->load(id, [&](LibraryLoadResult r) { loaded = std::move(r); });
    REQUIRE(pumpUntil([&] { return loaded.has_value(); }));
    REQUIRE(loaded->ok());
    CHECK(loaded->entry->pattern == first.pattern); // the library gives back what was saved, note for note
    CHECK(loaded->entry->pattern->info.referenceSetId == "default");
}

TEST_CASE("a removal alone brings the index up to date", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 6);
    {
        auto store = open(folder);
        REQUIRE(store->remove(saved[2]));
    }
    const auto loaded = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString());
    REQUIRE(loaded.ok);
    CHECK(loaded.records.size() == 5);
}

TEST_CASE("an entry gone from the folder makes the next start write the index again", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    const auto saved = fill(folder, 5);
    folder.folder.getChildFile(juce::String(saved[1]) + ".json").deleteFile();
    {
        auto store = open(folder); // nothing new, nothing changed: only a file less
        CHECK(store->parsedCount() == 0);
        CHECK(store->indexedCount() == 4);
    }
    const auto loaded = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString());
    REQUIRE(loaded.ok);
    CHECK(loaded.records.size() == 4);
}

TEST_CASE("an index with a bad record is written again without it", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    fill(folder, 4);
    nlohmann::json document = nlohmann::json::parse(indexFileOf(folder).loadFileAsString().toStdString());
    document["entries"].push_back(5); // not a record
    indexFileOf(folder).replaceWithText(juce::String::fromUTF8(document.dump().c_str()));
    {
        auto store = open(folder);
        CHECK(store->indexedCount() == 4);
        CHECK(store->parsedCount() == 0);
    }
    const auto loaded = loadLibraryIndex(indexFileOf(folder).loadFileAsString().toStdString());
    REQUIRE(loaded.ok);
    CHECK(loaded.skipped == 0);
    CHECK(loaded.records.size() == 4);
}

TEST_CASE("a store without a folder writes no file anywhere", "[library-plugin][index]") {
    juce::ScopedJuceInitialiser_GUI gui;
    const auto stray = juce::File::getCurrentWorkingDirectory().getChildFile(LibraryStore::kIndexFileName);
    REQUIRE_FALSE(stray.exists());
    {
        LibraryStore store{juce::File()};
        const auto id = store.savePattern("x", made(1), "", 0).id;
        store.rename(id, "y");
        store.remove(id);
        store.waitForIdle();
    }
    CHECK_FALSE(stray.exists());
}

TEST_CASE("the window has the look of the plug-in", "[library-plugin][dialog][look]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    LibraryDialog dialog(processor, processor.libraryAliveFlag(), [&]() -> LibraryStore& { return scoped.store; });
    auto& load = *find(dialog, "load");
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&dialog.getLookAndFeel()) != nullptr);
    CHECK(dynamic_cast<MidiMaidLookAndFeel*>(&load.getLookAndFeel()) != nullptr); // the children inherit it
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
TEST_CASE("snapshot of the library", "[.snapshot]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    fillSlot(processor, 0, 4);
    LibraryDialog dialog(processor, processor.libraryAliveFlag(), [&]() -> LibraryStore& { return scoped.store; });
    dynamic_cast<juce::TextButton*>(find(dialog, "saveSlot"))->onClick();
    dynamic_cast<juce::TextButton*>(find(dialog, "saveSet"))->onClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
    dynamic_cast<juce::ListBox*>(find(dialog, "list"))->selectRow(0);
    writeSnapshot(dialog, "library.png");
}


// ---- tags, rating, tempo range, search, filter and the preview (SPEC 3.12, v1.1, D-237) ------------------------------------

TEST_CASE("the meta data of an entry change the list at once and are in the file and the index", "[library-plugin][store][meta]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder folder;
    LibraryStore store{folder.folder};
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    const auto a = store.savePattern("a", made(1), "", 0);
    const auto b = store.savePattern("b", made(2), "", 0);
    store.waitForIdle();
    LibraryMeta meta;
    meta.tags = {" dark ", "Rolling", "dark"};
    meta.rating = 9;
    meta.bpmLow = 134;
    meta.bpmHigh = 126;
    CHECK(store.setMeta(a.id, meta));
    CHECK(store.info(a.id)->tags == std::vector<std::string>{"dark", "Rolling"}); // sanitized
    CHECK(store.info(a.id)->rating == 5);
    CHECK(store.info(a.id)->bpmLow == 126);
    CHECK(store.info(a.id)->bpmHigh == 134);
    CHECK(store.info(b.id)->tags.empty()); // the other one is left alone
    CHECK_FALSE(store.setMeta("nope", meta));
    store.waitForIdle();
    // the file and the index hold it: another store reads it back, without reading the file when the index is good
    LibraryStore again{folder.folder};
    REQUIRE(pumpUntil([&] { return again.ready(); }));
    REQUIRE(again.info(a.id).has_value());
    CHECK(again.info(a.id)->tags == std::vector<std::string>{"dark", "Rolling"});
    CHECK(again.info(a.id)->rating == 5);
    CHECK(again.info(a.id)->bpmHigh == 134);
    CHECK(again.indexedCount() == 2);
    CHECK(again.parsedCount() == 0);
    // the entry loads with its pattern and the meta data
    bool loaded = false;
    again.load(a.id, [&](LibraryLoadResult result) {
        loaded = result.ok() && result.entry->info.rating == 5 && result.entry->pattern.has_value();
    });
    CHECK(pumpUntil([&] { return loaded; }));
    // clearing
    CHECK(store.setMeta(a.id, {}));
    CHECK(store.info(a.id)->tags.empty());
    CHECK(store.info(a.id)->rating == 0);
    CHECK(store.info(a.id)->bpmHigh == 0);
    store.waitForIdle();
}

TEST_CASE("the meta data of the memory store follow too", "[library-plugin][store][meta]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    const auto saved = scoped.store.savePattern("x", made(3), "", 0);
    CHECK(scoped.store.setMeta(saved.id, {{"acid"}, 3, 130, 130}));
    CHECK(scoped.store.info(saved.id)->rating == 3);
    bool ok = false;
    scoped.store.load(saved.id, [&](LibraryLoadResult result) { ok = result.ok() && result.entry->info.tags.size() == 1; });
    CHECK(pumpUntil([&] { return ok; }));
}

TEST_CASE("the window rates, tags and filters", "[library-plugin][dialog][meta]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    const auto a = scoped.store.savePattern("Rolling", made(1), "", 0);
    const auto b = scoped.store.savePattern("Acid Line", made(2), "", 0);
    const auto c = scoped.store.savePattern("Pad", made(3), "", 0);
    scoped.store.setMeta(a.id, {{"dark", "warehouse"}, 5, 126, 132});
    scoped.store.setMeta(b.id, {{"acid"}, 3, 138, 150});
    LibraryDialog dialog(processor, processor.libraryAliveFlag(), [&]() -> LibraryStore& { return scoped.store; });
    auto& list = *dynamic_cast<juce::ListBox*>(find(dialog, "list"));
    auto& search = *dynamic_cast<juce::TextEditor*>(find(dialog, "search"));
    auto& stars = *dynamic_cast<juce::ComboBox*>(find(dialog, "stars"));
    auto& tags = *dynamic_cast<juce::TextEditor*>(find(dialog, "tags"));
    auto& bpmLow = *dynamic_cast<juce::TextEditor*>(find(dialog, "bpmLow"));
    auto& bpmHigh = *dynamic_cast<juce::TextEditor*>(find(dialog, "bpmHigh"));
    auto& apply = *dynamic_cast<juce::TextButton*>(find(dialog, "applyMeta"));
    auto& filterStars = *dynamic_cast<juce::ComboBox*>(find(dialog, "filterStars"));
    auto& filterTag = *dynamic_cast<juce::ComboBox*>(find(dialog, "filterTag"));
    auto& filterStyle = *dynamic_cast<juce::ComboBox*>(find(dialog, "filterStyle"));
    auto& filterBpm = *dynamic_cast<juce::TextEditor*>(find(dialog, "filterBpm"));
    CHECK(list.getListBoxModel()->getNumRows() == 3);
    // nothing selected: the fields of the entry are off
    CHECK_FALSE(tags.isEnabled());
    CHECK_FALSE(stars.isEnabled());
    CHECK_FALSE(apply.isEnabled());
    // the entry that is selected shows its data (the newest first: Pad, Acid Line, Rolling)
    list.selectRow(2);
    CHECK(tags.isEnabled());
    CHECK(tags.getText() == "dark, warehouse");
    CHECK(stars.getSelectedId() == 6);
    CHECK(bpmLow.getText() == "126");
    CHECK(bpmHigh.getText() == "132");
    list.selectRow(0); // Pad: nothing set
    CHECK(tags.getText().isEmpty());
    CHECK(stars.getSelectedId() == 1);
    CHECK(bpmLow.getText().isEmpty());
    // a rating is one click
    stars.setSelectedId(5, juce::sendNotificationSync); // four stars
    CHECK(scoped.store.info(c.id)->rating == 4);
    // tags and tempo with the button
    tags.setText("calm; Pad, calm", juce::dontSendNotification);
    bpmLow.setText("90", juce::dontSendNotification);
    bpmHigh.setText("100", juce::dontSendNotification);
    apply.onClick();
    CHECK(scoped.store.info(c.id)->tags == std::vector<std::string>{"calm", "Pad"});
    CHECK(scoped.store.info(c.id)->bpmLow == 90);
    CHECK(scoped.store.info(c.id)->bpmHigh == 100);
    CHECK(scoped.store.info(c.id)->rating == 4); // the stars stayed
    CHECK(scoped.store.info(c.id)->tags.size() == 2);

    // the search narrows the list (name, tag, prompt), and the count follows
    search.setText("acid", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 1; }));
    search.setText("WAREHOUSE", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 1; }));
    search.setText("nothing like it", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 0; }));
    CHECK_FALSE(find(dialog, "empty")->isVisible()); // the library is not empty, only the search finds nothing
    search.setText("", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 3; }));
    // stars
    filterStars.setSelectedId(5, juce::sendNotificationSync); // four stars and up
    CHECK(list.getListBoxModel()->getNumRows() == 2);
    filterStars.setSelectedId(6, juce::sendNotificationSync);
    CHECK(list.getListBoxModel()->getNumRows() == 1);
    filterStars.setSelectedId(1, juce::sendNotificationSync);
    // a tag from the library
    bool hasAcid = false;
    for (int i = 0; i < filterTag.getNumItems(); ++i) {
        if (filterTag.getItemText(i) == "acid") {
            filterTag.setSelectedItemIndex(i, juce::sendNotificationSync);
            hasAcid = true;
        }
    }
    CHECK(hasAcid);
    CHECK(list.getListBoxModel()->getNumRows() == 1);
    filterTag.setSelectedId(1, juce::sendNotificationSync);
    CHECK(list.getListBoxModel()->getNumRows() == 3);
    // a tempo
    filterBpm.setText("128", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 1; }));
    filterBpm.setText("95", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 1; }));
    filterBpm.setText("10", juce::sendNotificationSync);
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 0; }));
    filterBpm.setText("", juce::sendNotificationSync);
    // the style: all three are Peak Time here
    CHECK(filterStyle.getNumItems() == 2);
    filterStyle.setSelectedId(2, juce::sendNotificationSync);
    CHECK(list.getListBoxModel()->getNumRows() == 3);
    filterStyle.setSelectedId(1, juce::sendNotificationSync);
    // a change in the store (a rating from elsewhere) shows through the filter
    filterStars.setSelectedId(6, juce::sendNotificationSync);
    CHECK(list.getListBoxModel()->getNumRows() == 1);
    scoped.store.setMeta(b.id, {{"acid"}, 5, 138, 150});
    CHECK(pumpUntil([&] { return list.getListBoxModel()->getNumRows() == 2; }));
}

TEST_CASE("the preview plays the entry in the slot with the internal clock and gives the slot back", "[library-plugin][processor][preview]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    // the slot plays 41, the entry 50
    processor.editSlots([](SlotBank& bank) {
        Pattern pattern = makeEmptyPattern(1, "peak_time");
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = 41;
        note.startTick = 0;
        note.lengthTicks = 240;
        pattern.voices[0].notes.push_back(note);
        REQUIRE(bank.setResult(0, pattern));
    });
    Pattern entryPattern = makeEmptyPattern(1, "peak_time");
    Note entryNote;
    entryNote.id = allocateNoteId(entryPattern);
    entryNote.pitch = 50;
    entryNote.startTick = 0;
    entryNote.lengthTicks = 240;
    entryPattern.voices[0].notes.push_back(entryNote);
    const auto saved = scoped.store.savePattern("entry", entryPattern, "", 0);

    class Head : public juce::AudioPlayHead {
    public:
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    } head;
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;
    processor.setRateAndBufferSizeDetails(kRate, kBlock);
    processor.setPlayHead(&head);
    processor.prepareToPlay(kRate, kBlock);
    head.info.setIsPlaying(false);
    head.info.setBpm(120.0);
    head.info.setPpqPosition(0.0);
    juce::AudioBuffer<float> audio(2, kBlock);
    juce::MidiBuffer midi;
    const auto pitchesOf = [&](int blocks) {
        std::set<int> pitches;
        for (int i = 0; i < blocks; ++i) {
            audio.clear();
            midi.clear();
            processor.processBlock(audio, midi);
            for (const auto metadata : midi) {
                if (metadata.getMessage().isNoteOn()) {
                    pitches.insert(metadata.getMessage().getNoteNumber());
                }
            }
        }
        return pitches;
    };
    CHECK(pitchesOf(10).empty()); // the host stands still
    CHECK_FALSE(processor.libraryPreviewing());
    REQUIRE(processor.previewLibraryEntry(saved.id));
    REQUIRE(pumpUntil([&] { return processor.libraryPreviewing(); }));
    const auto heard = pitchesOf(250);
    CHECK(heard == std::set<int>{50}); // the entry, not the slot
    CHECK(processor.slotsSnapshot().slot(0)->pattern->voices[0].notes[0].pitch == 41); // the slot is untouched
    processor.stopLibraryPreview();
    CHECK_FALSE(processor.libraryPreviewing());
    CHECK_FALSE(processor.previewing());
    // the slot comes back: it plays once the preview of the slot runs
    processor.setPreview(true);
    CHECK(pitchesOf(250) == std::set<int>{41});
    processor.setPreview(false);
    // an unknown entry and a voice
    CHECK_FALSE(processor.previewLibraryEntry("nope"));
    InstrumentProcessor voice("Voice");
    auto settings = voice.instanceSettings();
    settings.role = InstanceRole::Voice;
    voice.setInstanceSettings(settings);
    CHECK_FALSE(voice.previewLibraryEntry(saved.id));
}

TEST_CASE("the start of the host ends the preview of an entry and gives the slot back", "[library-plugin][processor][preview]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    fillSlot(processor, 0, 5);
    const auto saved = scoped.store.savePattern("entry", made(6), "", 0);
    class Head : public juce::AudioPlayHead {
    public:
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    } head;
    processor.setRateAndBufferSizeDetails(48000.0, 512);
    processor.setPlayHead(&head);
    processor.prepareToPlay(48000.0, 512);
    head.info.setIsPlaying(false);
    head.info.setBpm(120.0);
    head.info.setPpqPosition(0.0);
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    REQUIRE(processor.previewLibraryEntry(saved.id));
    REQUIRE(pumpUntil([&] { return processor.libraryPreviewing(); }));
    processor.processBlock(audio, midi);
    head.info.setIsPlaying(true); // the host starts
    head.info.setPpqPosition(8.0);
    processor.processBlock(audio, midi);
    CHECK_FALSE(processor.previewing());
    processor.pollPreview();
    CHECK_FALSE(processor.libraryPreviewing());
    // the slot of the user plays again, not the entry (the overlay is gone, not only hidden): the same as a processor
    // that never previewed
    InstrumentProcessor reference("Reference");
    fillSlot(reference, 0, 5);
    reference.setRateAndBufferSizeDetails(48000.0, 512);
    reference.setPlayHead(&head);
    reference.prepareToPlay(48000.0, 512);
    const auto play = [&](InstrumentProcessor& target) {
        std::vector<int> heard;
        for (int block = 0; block < 400; ++block) {
            head.info.setPpqPosition(8.0 + block * 512.0 / 48000.0 * 2.0);
            audio.clear();
            midi.clear();
            target.processBlock(audio, midi);
            for (const auto metadata : midi) {
                if (metadata.getMessage().isNoteOn()) {
                    heard.push_back(metadata.getMessage().getNoteNumber());
                }
            }
        }
        return heard;
    };
    const auto expected = play(reference);
    CHECK_FALSE(expected.empty());
    CHECK(play(processor) == expected);
}

TEST_CASE("the window starts and stops the preview and gives the slot back when it goes", "[library-plugin][dialog][preview]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ScopedStore scoped;
    InstrumentProcessor processor("Test");
    scoped.store.savePattern("entry", made(7), "", 0);
    {
        LibraryDialog dialog(processor, processor.libraryAliveFlag(), [&]() -> LibraryStore& { return scoped.store; });
        auto& list = *dynamic_cast<juce::ListBox*>(find(dialog, "list"));
        auto& preview = *dynamic_cast<juce::TextButton*>(find(dialog, "preview"));
        CHECK_FALSE(preview.isEnabled()); // nothing selected
        list.selectRow(0);
        CHECK(preview.isEnabled());
        CHECK(preview.getButtonText() == "Vorhören");
        preview.onClick();
        REQUIRE(pumpUntil([&] { return processor.libraryPreviewing(); }));
        CHECK(pumpUntil([&] { return preview.getButtonText() == "Stopp"; }));
        preview.onClick();
        CHECK_FALSE(processor.libraryPreviewing());
        CHECK(pumpUntil([&] { return preview.getButtonText() == "Vorhören"; }));
        preview.onClick(); // and again: the dialog closes while it plays
        REQUIRE(pumpUntil([&] { return processor.libraryPreviewing(); }));
    }
    CHECK_FALSE(processor.libraryPreviewing()); // the window is gone: the slot is back
    CHECK_FALSE(processor.previewing());
}
