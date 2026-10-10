#include "NoUserSettings.h"
#include "core/MidiFile.h"
#include "core/MidiImport.h"
#include "core/PatternGenerator.h"
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
