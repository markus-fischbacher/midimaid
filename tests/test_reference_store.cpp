#include "core/MidiFile.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/ReferenceStore.h"

#include <atomic>
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

std::vector<uint8_t> bassFile(int shift = 0, uint8_t numerator = 4, uint32_t bars = 4) {
    const int roots[] = {45, 45, 48, 43};
    std::vector<PatternNote> list;
    for (uint32_t bar = 0; bar < bars; ++bar) {
        for (uint32_t i = 0; i < 4; ++i) {
            list.push_back({bar * kTicksPerBar + i * 960, 200, 1, static_cast<uint8_t>(roots[bar % 4] + shift), 100});
        }
    }
    MidiFileOptions options;
    options.timeSigNumerator = numerator;
    return writeMidiFile(PatternView{list.data(), list.size(), bars * kTicksPerBar}, options);
}

struct TempFolder {
    TempFolder() : folder(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("mm_ref_" + juce::Uuid().toString())) {
        folder.createDirectory();
    }
    ~TempFolder() { folder.deleteRecursively(); }
    juce::File write(const juce::String& relative, const std::vector<uint8_t>& bytes) const {
        const auto file = folder.getChildFile(relative);
        file.getParentDirectory().createDirectory();
        file.replaceWithData(bytes.data(), bytes.size());
        return file;
    }
    juce::File folder;
};

bool idle(const ReferenceStore& store) {
    return !store.progress().running;
}

/// Starts an import and waits until the store says it is done.
void importAndWait(ReferenceStore& store, const std::string& style, std::vector<juce::File> paths) {
    store.importFiles(style, std::move(paths));
    REQUIRE(pumpUntil([&] { return idle(store); }));
    store.waitForIdle();
}

} // namespace

TEST_CASE("an imported file is analysed into the set of the style and saved", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto file = data.write("in/bass.mid", bassFile());
    {
        ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
        REQUIRE(pumpUntil([&] { return store.ready(); }));
        CHECK(store.get("peak_time").items.empty());
        std::atomic<int> notified{0};
        store.setOnChanged([&] { ++notified; });
        importAndWait(store, "peak_time", {file});
        const auto set = store.get("peak_time");
        REQUIRE(set.items.size() == 1);
        CHECK(set.items[0].name == "bass.mid");
        CHECK(set.items[0].role == TrackRole::Bass);
        CHECK(set.items[0].analysis.key.root == 9);
        const auto progress = store.progress();
        CHECK(progress.total == 1);
        CHECK(progress.done == 1);
        CHECK(progress.report.added == 1);
        CHECK(store.get("melodic_techno").items.empty()); // one set per style
        REQUIRE(pumpUntil([&] { return notified.load() > 0; }));
    }
    // a new process reads it back
    ReferenceStore again(data.folder.getChildFile("refs"), embeddedStyles());
    REQUIRE(pumpUntil([&] { return again.ready(); }));
    REQUIRE(again.get("peak_time").items.size() == 1);
    CHECK(again.get("peak_time").items[0].analysis.key.root == 9);
    CHECK(data.folder.getChildFile("refs/peak_time.json").existsAsFile());
}

TEST_CASE("a file is in the set once, also under another name", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto first = data.write("a.mid", bassFile());
    const auto copy = data.write("copy of a.mid", bassFile());
    const auto other = data.write("b.mid", bassFile(2));
    ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
    importAndWait(store, "peak_time", {first});
    importAndWait(store, "peak_time", {first});
    CHECK(store.progress().report.duplicates == 1);
    CHECK(store.progress().report.added == 0);
    importAndWait(store, "peak_time", {copy, other});
    CHECK(store.progress().report.duplicates == 1);
    CHECK(store.progress().report.added == 1);
    CHECK(store.get("peak_time").items.size() == 2);
}

TEST_CASE("folders are searched for MIDI files, deeper ones too", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    data.write("set/one.mid", bassFile());
    data.write("set/deep/er/two.MID", bassFile(1));
    data.write("set/three.midi", bassFile(2));
    data.write("set/notes.txt", {'h', 'i'});
    data.write("set/.hidden.mid", bassFile(3));
    ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
    importAndWait(store, "peak_time", {data.folder.getChildFile("set")});
    const auto set = store.get("peak_time");
    CHECK(store.progress().total == 3);
    REQUIRE(set.items.size() == 3);
    // the order does not depend on the file system: sorted by path
    CHECK(set.items[0].name == "two.MID");
    CHECK(set.items[1].name == "one.mid");
    CHECK(set.items[2].name == "three.midi");
}

TEST_CASE("files that cannot be used are counted and the rest goes in", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto good = data.write("good.mid", bassFile());
    const auto waltz = data.write("waltz.mid", bassFile(0, 3));
    const auto broken = data.write("broken.mid", {'n', 'o', 'p', 'e'});
    const auto missing = data.folder.getChildFile("missing.mid");
    ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
    importAndWait(store, "peak_time", {good, waltz, broken, missing});
    const auto report = store.progress().report;
    CHECK(report.added == 1);
    CHECK(report.notFourFour == 1);
    CHECK(report.unreadable == 1); // the missing file is not even looked at
    CHECK(store.get("peak_time").items.size() == 1);
    CHECK(store.progress().done == 3);
}

TEST_CASE("the switches of an item are saved", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto file = data.write("a.mid", bassFile());
    {
        ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
        importAndWait(store, "peak_time", {file});
        const auto id = store.get("peak_time").items[0].id;
        CHECK(store.setActive("peak_time", id, false));
        CHECK(store.setWeight("peak_time", id, 5));
        CHECK(store.setRole("peak_time", id, TrackRole::Chords));
        CHECK_FALSE(store.setActive("peak_time", "nope", true));
        CHECK_FALSE(store.setActive("melodic_techno", id, true));
        CHECK_FALSE(store.setWeight("../x", id, 2));
        store.waitForIdle();
    }
    ReferenceStore again(data.folder.getChildFile("refs"), embeddedStyles());
    REQUIRE(pumpUntil([&] { return again.ready(); }));
    const auto set = again.get("peak_time");
    REQUIRE(set.items.size() == 1);
    CHECK_FALSE(set.items[0].active);
    CHECK(set.items[0].weight == 5);
    CHECK(set.items[0].role == TrackRole::Chords);
    CHECK(again.remove("peak_time", set.items[0].id));
    again.waitForIdle();
    ReferenceStore third(data.folder.getChildFile("refs"), embeddedStyles());
    REQUIRE(pumpUntil([&] { return third.ready(); }));
    CHECK(third.get("peak_time").items.empty());
}

TEST_CASE("a bad set file gives an empty set and other files in the folder are ignored", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    data.write("refs/peak_time.json", {'{', 'x'});
    data.write("refs/Bad Name.json", {'{', '}'});
    data.write("refs/notes.txt", {'h', 'i'});
    ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
    REQUIRE(pumpUntil([&] { return store.ready(); }));
    CHECK(store.get("peak_time").items.empty());
    CHECK(store.get("Bad Name").items.empty());
}

TEST_CASE("a store without folder keeps its sets in memory", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto file = data.write("a.mid", bassFile());
    ReferenceStore store(juce::File(), embeddedStyles());
    CHECK(store.ready());
    CHECK_FALSE(store.enabled());
    importAndWait(store, "peak_time", {file});
    CHECK(store.get("peak_time").items.size() == 1);
    CHECK(store.setActive("peak_time", store.get("peak_time").items[0].id, false));
    store.waitForIdle();
}

TEST_CASE("an invalid style or nothing to import does nothing", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto file = data.write("a.mid", bassFile());
    ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
    store.importFiles("../etc", {file});
    store.importFiles("", {file});
    store.importFiles("peak_time", {});
    pumpUntil([] { return false; }, 100);
    store.waitForIdle();
    CHECK_FALSE(store.progress().running);
    CHECK(store.progress().total == 0);
    CHECK_FALSE(data.folder.getChildFile("refs").exists());
}

TEST_CASE("an import can be cancelled and what was done stays", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    std::vector<juce::File> files;
    for (int i = 0; i < 60; ++i) {
        files.push_back(data.write("f" + juce::String(i) + ".mid", bassFile(i % 12, 4, 1 + static_cast<uint32_t>(i % 4))));
    }
    ReferenceStore store(data.folder.getChildFile("refs"), embeddedStyles());
    store.importFiles("peak_time", files);
    store.cancelImport();
    REQUIRE(pumpUntil([&] { return idle(store); }));
    store.waitForIdle();
    CHECK(store.progress().done <= store.progress().total);
    CHECK(store.get("peak_time").items.size() <= 60);
    // the next import works again
    importAndWait(store, "peak_time", {files.front()});
    CHECK(store.progress().done == 1);
}

TEST_CASE("a file over the size limit is not read", "[reference-store]") {
    juce::ScopedJuceInitialiser_GUI gui;
    TempFolder data;
    const auto huge = data.write("huge.mid", std::vector<uint8_t>(mm::core::kMaxMidiFileBytes + 1, 0));
    ReferenceStore store(juce::File(), embeddedStyles());
    importAndWait(store, "peak_time", {huge});
    CHECK(store.progress().report.unreadable == 1);
    CHECK(store.get("peak_time").items.empty());
}
