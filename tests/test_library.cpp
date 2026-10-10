#include "core/Library.h"
#include "core/MidiImport.h"
#include "core/PatternGenerator.h"
#include "core/PatternJson.h"
#include "core/PlaybackRender.h"
#include "core/SlotBankJson.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace mm::core;
using nlohmann::json;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

namespace {

StyleProfile shipped(const std::string& name = "peak_time") {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

Pattern made(uint64_t seed, uint32_t bars = 4, const std::string& style = "peak_time") {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    request.settings.energyPct = 60;
    const auto result = generatePattern(shipped(style), request);
    REQUIRE(result.success);
    Pattern pattern = result.pattern;
    pattern.info.prompt = "dark \"rolling\" bass";
    return pattern;
}

SlotBank bankWith(std::initializer_list<size_t> filled) {
    SlotBank bank;
    uint64_t seed = 50;
    for (const size_t index : filled) {
        REQUIRE(bank.setResult(index, made(seed++)));
        REQUIRE(bank.setName(index, "Slot " + std::to_string(index)));
        REQUIRE(bank.setColor(index, static_cast<uint8_t>(index % 8 + 1)));
    }
    return bank;
}

} // namespace

// ---- ids and names ------------------------------------------------------------------------------------------------

TEST_CASE("only plain lower-case ids reach the file system", "[library]") {
    for (const char* ok : {"a", "0", "pattern-1_b", "20261010t120000-ab12"}) {
        CHECK(isValidLibraryId(ok));
    }
    CHECK(isValidLibraryId(std::string(64, 'a')));
    CHECK_FALSE(isValidLibraryId(std::string(65, 'a')));
    for (const char* bad : {"", "A", "a b", "a.json", "../x", "a/b", "a\\b", "ü", ".", "a\n"}) {
        CHECK_FALSE(isValidLibraryId(bad));
    }
    CHECK_FALSE(isValidLibraryId(std::string_view("a\0b", 3)));
}

TEST_CASE("a name is cleaned, trimmed and cut at a character boundary", "[library]") {
    CHECK(sanitizeLibraryName("  Drop  ") == "Drop");
    CHECK(sanitizeLibraryName("a\tb\nc\x7f") == "abc");
    CHECK(sanitizeLibraryName("a  b") == "a  b"); // inner spaces stay
    CHECK(sanitizeLibraryName("") == "");
    CHECK(sanitizeLibraryName("   ") == "");
    CHECK(sanitizeLibraryName("", "peak_time") == "peak_time");
    CHECK(sanitizeLibraryName("\t", "  fallback ") == "fallback");
    CHECK(sanitizeLibraryName("x", "fallback") == "x");
    const std::string longName(100, 'a');
    CHECK(sanitizeLibraryName(longName).size() == kMaxLibraryNameBytes);
    // 63 bytes of text and a two-byte character: the character does not fit and is dropped whole
    const std::string edge = std::string(63, 'a') + "\xC3\xBC" + "tail";
    CHECK(sanitizeLibraryName(edge) == std::string(63, 'a'));
    // exactly at the limit: kept
    const std::string fits = std::string(62, 'a') + "\xC3\xBC";
    CHECK(sanitizeLibraryName(fits) == fits);
    // trailing spaces left by the cut are trimmed
    CHECK(sanitizeLibraryName(std::string(63, 'a') + " b") == std::string(63, 'a'));
}

// ---- entries --------------------------------------------------------------------------------------------------------

TEST_CASE("a pattern entry carries the data the list shows", "[library]") {
    const Pattern pattern = made(7);
    const auto entry = makePatternEntry("p1", "  My drop ", pattern, "Intro", 3, 1234567);
    CHECK(entry.info.id == "p1");
    CHECK(entry.info.kind == LibraryKind::Pattern);
    CHECK(entry.info.name == "My drop");
    CHECK(entry.info.styleId == "peak_time");
    CHECK(entry.info.root == pattern.context.root);
    CHECK(entry.info.scaleId == pattern.context.scaleId);
    CHECK(entry.info.seed == pattern.info.seed);
    CHECK(entry.info.prompt == "dark \"rolling\" bass");
    CHECK(entry.info.lengthBars == 4);
    CHECK(entry.info.slotCount == 1);
    CHECK(entry.info.createdUnixMs == 1234567);
    CHECK(entry.slotName == "Intro");
    CHECK(entry.slotColor == 3);
    // an empty name becomes the style, a colour beyond the palette is cut
    CHECK(makePatternEntry("p1", "", pattern, "", 200, 0).info.name == "peak_time");
    CHECK(makePatternEntry("p1", "", pattern, "", 200, 0).slotColor == kMaxColor);
}

TEST_CASE("a pattern entry survives the file exactly", "[library]") {
    const Pattern pattern = made(11, 8);
    const auto entry = makePatternEntry("p-1", "Peak", pattern, "Break \xC3\xBC", 5, 99);
    const auto text = libraryEntryToString(entry);
    CHECK(libraryEntryToString(entry) == text); // equal entries give equal text
    const auto loaded = loadLibraryEntry(text, "p-1");
    REQUIRE(loaded.ok());
    CHECK(loaded.error.empty());
    CHECK_FALSE(loaded.fromNewerVersion);
    CHECK(loaded.entry->info == entry.info);
    CHECK(loaded.entry->pattern == pattern);
    CHECK(loaded.entry->slotName == "Break \xC3\xBC");
    CHECK(loaded.entry->slotColor == 5);
    CHECK(libraryEntryToString(*loaded.entry) == text);
}

TEST_CASE("a set entry holds the filled slots, with names and colours, and no history", "[library]") {
    SlotBank bank = bankWith({1, 4, 15});
    REQUIRE(bank.setResult(4, made(90))); // a second result in slot 4: history
    const auto entry = makeSetEntry("s1", "My set", bank, 5);
    REQUIRE(entry.has_value());
    CHECK(entry->info.kind == LibraryKind::Set);
    CHECK(entry->info.slotCount == 3);
    CHECK(entry->info.styleId == "peak_time"); // of the first filled slot
    CHECK(entry->info.seed == bank.slot(1)->pattern->info.seed);
    CHECK(entry->info.lengthBars == bank.slot(1)->pattern->lengthBars);

    const auto loaded = loadLibraryEntry(libraryEntryToString(*entry), "s1");
    REQUIRE(loaded.ok());
    REQUIRE(loaded.entry->set.has_value());
    const SlotBank& set = *loaded.entry->set;
    for (size_t i = 0; i < kSlotCount; ++i) {
        const bool filled = i == 1 || i == 4 || i == 15;
        CHECK(set.slot(i)->pattern.has_value() == filled);
        if (filled) {
            CHECK(set.slot(i)->pattern == bank.slot(i)->pattern);
            CHECK(set.slot(i)->name == bank.slot(i)->name);
            CHECK(set.slot(i)->color == bank.slot(i)->color);
            CHECK(set.slot(i)->history.size() <= 1);
        }
    }
    CHECK(loaded.entry->info == entry->info);
    CHECK(loaded.entry->info.slotCount == 3);
}

TEST_CASE("a set without a pattern is no entry", "[library]") {
    CHECK_FALSE(makeSetEntry("s", "empty", SlotBank{}, 0).has_value());
}

// ---- loading what is not right -------------------------------------------------------------------------------------

TEST_CASE("an entry file that is not usable gives a reason and never crashes", "[library]") {
    const auto text = libraryEntryToString(makePatternEntry("p", "x", made(3), "", 0, 0));
    CHECK_FALSE(loadLibraryEntry(text, "../evil").ok());
    CHECK(loadLibraryEntry(text, "").error.find("id") == 0);
    CHECK_FALSE(loadLibraryEntry("", "p").ok());
    CHECK_FALSE(loadLibraryEntry("not json", "p").ok());
    CHECK_FALSE(loadLibraryEntry("[1,2]", "p").ok());
    CHECK_FALSE(loadLibraryEntry(text.substr(0, text.size() / 2), "p").ok());

    const auto mutated = [&](const std::function<void(json&)>& change) {
        json document = json::parse(text);
        change(document);
        return loadLibraryEntry(document.dump(), "p");
    };
    CHECK(mutated([](json& d) { d.erase("libraryVersion"); }).error.find("libraryVersion") == 0);
    CHECK(mutated([](json& d) { d["libraryVersion"] = 0; }).error.find("libraryVersion") == 0);
    CHECK(mutated([](json& d) { d["libraryVersion"] = "1"; }).error.find("libraryVersion") == 0);
    CHECK(mutated([](json& d) { d["kind"] = "folder"; }).error.find("kind") == 0);
    CHECK(mutated([](json& d) { d.erase("kind"); }).error.find("kind") == 0);
    CHECK(mutated([](json& d) { d.erase("pattern"); }).error.find("pattern") == 0);
    CHECK(mutated([](json& d) { d["pattern"] = json::object(); }).error.find("pattern") == 0);
    CHECK(mutated([](json& d) { d["pattern"]["lengthBars"] = 3; }).error.find("pattern") == 0);
}

TEST_CASE("a newer entry loads as far as understood", "[library]") {
    json document = json::parse(libraryEntryToString(makePatternEntry("p", "x", made(3), "", 0, 0)));
    document["libraryVersion"] = kLibraryEntryVersion + 1;
    document["somethingNew"] = {{"a", 1}};
    const auto loaded = loadLibraryEntry(document.dump(), "p");
    REQUIRE(loaded.ok());
    CHECK(loaded.fromNewerVersion);
    CHECK_FALSE(loadLibraryEntry(libraryEntryToString(makePatternEntry("p", "x", made(3), "", 0, 0)), "p")
                    .fromNewerVersion);
}

TEST_CASE("the id comes from the file name and the values are kept inside their ranges", "[library]") {
    json document = json::parse(libraryEntryToString(makePatternEntry("original", "x", made(3), "", 0, 0)));
    document["id"] = "other";
    document["root"] = 99;
    document["bars"] = 500;
    document["slotCount"] = -4;
    document["name"] = std::string(200, 'n');
    document["slotColor"] = 77;
    document["seed"] = "not a number";
    const auto loaded = loadLibraryEntry(document.dump(), "from-file");
    REQUIRE(loaded.ok());
    const auto& info = loaded.entry->info;
    CHECK(info.id == "from-file");
    CHECK(info.root == 11);
    CHECK(info.lengthBars == 16);
    CHECK(info.slotCount == 0);
    CHECK(info.name.size() == kMaxLibraryNameBytes);
    CHECK(info.seed == 0);
    CHECK(loaded.entry->slotColor == kMaxColor);
}

TEST_CASE("a set with a broken slot loads the others and says which one", "[library]") {
    const auto entry = makeSetEntry("s", "set", bankWith({0, 1, 2}), 0);
    REQUIRE(entry.has_value());
    json document = json::parse(libraryEntryToString(*entry));
    document["slots"]["slots"][1]["pattern"] = json::object();
    const auto loaded = loadLibraryEntry(document.dump(), "s");
    REQUIRE(loaded.ok());
    CHECK(loaded.problems.size() == 1);
    CHECK(loaded.entry->set->slot(0)->pattern.has_value());
    CHECK_FALSE(loaded.entry->set->slot(1)->pattern.has_value());
    CHECK(loaded.entry->set->slot(2)->pattern.has_value());
    CHECK(loaded.entry->info.slotCount == 2); // counted from what loaded

    // nothing left: no entry
    document["slots"]["slots"][0]["pattern"] = json::object();
    document["slots"]["slots"][2]["pattern"] = json::object();
    const auto none = loadLibraryEntry(document.dump(), "s");
    CHECK_FALSE(none.ok());
    CHECK(none.error.find("slots") == 0);
    document.erase("slots");
    CHECK(loadLibraryEntry(document.dump(), "s").error.find("slots") == 0);
}

// ---- export ---------------------------------------------------------------------------------------------------------

TEST_CASE("the exported file has the notes the engine plays, on the channels of the voices", "[library][export]") {
    const auto style = shipped();
    const Pattern pattern = made(21, 2);
    const auto bytes = exportPatternMidi(pattern, style, 133.0);
    REQUIRE_FALSE(bytes.empty());
    const auto read = readMidiFile(bytes);
    REQUIRE(read.ok());
    size_t expected = 0;
    for (size_t voice = 0; voice < pattern.voices.size(); ++voice) {
        expected += renderVoiceForPlayback(pattern, style, voice).notes.size();
    }
    CHECK(read.clip.notes.size() == expected);
    CHECK(read.clip.lengthTicks == 2 * kTicksPerBar);
    CHECK(read.clip.bpm > 132.9);
    CHECK(read.clip.bpm < 133.1);
    for (const auto& voice : pattern.voices) {
        const auto count = std::count_if(read.clip.notes.begin(), read.clip.notes.end(),
                                         [&](const MidiClipNote& n) { return n.channel == voice.midiChannel; });
        CHECK(count > 0);
    }
    // a pattern without notes gives no file
    Pattern empty = pattern;
    for (auto& voice : empty.voices) {
        voice.notes.clear();
    }
    CHECK(exportPatternMidi(empty, style, 120.0).empty());
}

TEST_CASE("a newer version inside the entry counts as a newer entry", "[library]") {
    auto text = libraryEntryToString(makePatternEntry("p", "x", made(3), "", 0, 0));
    json document = json::parse(text);
    document["pattern"]["stateVersion"] = kPatternStateVersion + 1;
    const auto pattern = loadLibraryEntry(document.dump(), "p");
    REQUIRE(pattern.ok());
    CHECK(pattern.fromNewerVersion);

    const auto set = makeSetEntry("s", "s", bankWith({0}), 0);
    REQUIRE(set.has_value());
    json setDocument = json::parse(libraryEntryToString(*set));
    setDocument["slots"]["stateVersion"] = kSlotBankStateVersion + 1;
    const auto loaded = loadLibraryEntry(setDocument.dump(), "s");
    REQUIRE(loaded.ok());
    CHECK(loaded.fromNewerVersion);
}
