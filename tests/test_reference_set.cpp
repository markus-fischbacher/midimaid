#include "core/MidiFile.h"
#include "core/ReferenceSet.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

using namespace mm::core;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

namespace {

StyleProfile shipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

/// A bass line A A C G on channel `channel`: `perBar` notes per bar.
void addBass(std::vector<PatternNote>& list, uint8_t channel, uint32_t bars, uint32_t perBar = 4, int shift = 0) {
    const int roots[] = {45, 45, 48, 43};
    for (uint32_t bar = 0; bar < bars; ++bar) {
        for (uint32_t i = 0; i < perBar; ++i) {
            list.push_back({bar * kTicksPerBar + i * (kTicksPerBar / perBar), 200, channel,
                            static_cast<uint8_t>(roots[bar % 4] + shift), static_cast<uint8_t>(90 + i)});
        }
    }
}

std::vector<uint8_t> file(std::vector<PatternNote> list, uint32_t bars, uint8_t numerator = 4) {
    std::stable_sort(list.begin(), list.end(),
                     [](const PatternNote& a, const PatternNote& b) { return a.startTick < b.startTick; });
    MidiFileOptions options;
    options.timeSigNumerator = numerator;
    return writeMidiFile(PatternView{list.data(), list.size(), bars * kTicksPerBar}, options);
}

} // namespace

TEST_CASE("a file with one track gives one analysed item", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    ReferenceImportReport report;
    const auto items = itemsFromMidiFile("bass.mid", file(list, 4), style, report);
    REQUIRE(items.size() == 1);
    const auto& item = items[0];
    CHECK(item.name == "bass.mid");
    CHECK(item.role == TrackRole::Bass);
    CHECK(item.lengthBars == 4);
    CHECK(item.notes.size() == 16);
    CHECK(item.active);
    CHECK(item.weight == kDefaultReferenceWeight);
    CHECK(item.analysis.key.root == 9);
    CHECK(item.analysis.lengthBars == 4);
    CHECK(item.analysis.noteCount == 16);
    CHECK(item.analysis.progression.size() >= 4);
    CHECK(item.id.size() == 24 + 2); // hash, "-" and the channel
    CHECK(item.id.substr(24) == "-1");
    CHECK(report == ReferenceImportReport{});
}

TEST_CASE("a file with several tracks is taken apart by channel", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    for (uint32_t bar = 0; bar < 4; ++bar) { // a lead on channel 3
        for (uint32_t i = 0; i < 8; ++i) {
            list.push_back({bar * kTicksPerBar + i * 480, 200, 3, static_cast<uint8_t>(69 + (i % 3) * 3), 100});
        }
    }
    for (uint32_t step = 0; step < 16; ++step) { // drums on channel 10
        list.push_back({step * 240, 60, 10, static_cast<uint8_t>(step % 4 == 0 ? 36 : 42), 90});
    }
    ReferenceImportReport report;
    const auto items = itemsFromMidiFile("track.mid", file(list, 4), style, report);
    REQUIRE(items.size() == 3);
    CHECK(items[0].name == "track.mid (1)"); // several tracks: the channel is part of the name
    CHECK(items[1].name == "track.mid (3)");
    CHECK(items[2].name == "track.mid (10)");
    CHECK(items[0].role == TrackRole::Bass);
    CHECK(items[1].role == TrackRole::Melody);
    CHECK(items[2].role == TrackRole::Drums);
    CHECK(items[0].id != items[1].id);
    CHECK(items[0].id.substr(0, 24) == items[1].id.substr(0, 24)); // one file, one hash
    CHECK(items[1].notes.size() == 32);
    CHECK(items[2].lengthBars == 4); // the length of the file: one bar of drums plus the rest of the clip
}

TEST_CASE("tracks that are too small, files that are not 4/4 or not MIDI are skipped and counted", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    list.push_back({0, 200, 5, 60, 90});
    list.push_back({960, 200, 5, 62, 90}); // channel 5: two notes only
    ReferenceImportReport report;
    CHECK(itemsFromMidiFile("a.mid", file(list, 4), style, report).size() == 1);
    CHECK(report.tooSmall == 1);

    std::vector<PatternNote> waltz;
    addBass(waltz, 1, 4);
    CHECK(itemsFromMidiFile("waltz.mid", file(waltz, 4, 3), style, report).empty());
    CHECK(report.notFourFour == 1);

    CHECK(itemsFromMidiFile("x.mid", {'n', 'o', 'p', 'e'}, style, report).empty());
    CHECK(itemsFromMidiFile("e.mid", {}, style, report).empty());
    CHECK(report.unreadable == 2);
    CHECK(report.added == 0);
}

TEST_CASE("a clip over 16 bars is cut and counted", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 20);
    ReferenceImportReport report;
    const auto items = itemsFromMidiFile("long.mid", file(list, 20), style, report);
    REQUIRE(items.size() == 1);
    CHECK(items[0].lengthBars == 16);
    CHECK(items[0].notes.size() == 64);
    CHECK(report.truncated == 1);
}

TEST_CASE("the analysis is the one of the style's scales and does not depend on the key", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> low;
    std::vector<PatternNote> high;
    addBass(low, 1, 4);
    addBass(high, 1, 4, 4, 5);
    ReferenceImportReport report;
    const auto a = itemsFromMidiFile("a.mid", file(low, 4), style, report);
    const auto b = itemsFromMidiFile("b.mid", file(high, 4), style, report);
    REQUIRE(a.size() == 1);
    REQUIRE(b.size() == 1);
    CHECK(b[0].analysis.key.root == (a[0].analysis.key.root + 5) % 12);
    CHECK(b[0].analysis.progression.size() == a[0].analysis.progression.size());
    CHECK(a[0].id != b[0].id); // other bytes, other file
}

TEST_CASE("the hash tells equal files and different files apart", "[reference-set]") {
    const std::vector<uint8_t> a = {1, 2, 3};
    const std::vector<uint8_t> b = {1, 2, 4};
    const std::vector<uint8_t> c = {1, 2, 3, 0};
    CHECK(fileHash(a.data(), a.size()) == fileHash(a.data(), a.size()));
    CHECK(fileHash(a.data(), a.size()) != fileHash(b.data(), b.size()));
    CHECK(fileHash(a.data(), a.size()) != fileHash(c.data(), c.size()));
    CHECK(fileHash(a.data(), a.size()).size() == 24);
    CHECK(fileHash(nullptr, 0).size() == 24);
}

TEST_CASE("items are added once and the set is limited", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    const auto bytes = file(list, 4);
    ReferenceImportReport scratch;
    auto items = itemsFromMidiFile("bass.mid", bytes, style, scratch);
    REQUIRE(items.size() == 1);

    ReferenceSet set;
    set.styleId = "peak_time";
    ReferenceImportReport report;
    addItems(set, items, report);
    CHECK(report.added == 1);
    CHECK(containsFile(set, fileHash(bytes.data(), bytes.size())));
    CHECK_FALSE(containsFile(set, "0000000000000000" "00000000"));
    CHECK_FALSE(containsFile(set, fileHash(bytes.data(), bytes.size()).substr(0, 20))); // not a prefix of the hash

    addItems(set, items, report);
    CHECK(report.added == 1);
    CHECK(report.duplicates == 1);
    CHECK(set.items.size() == 1);

    // up to the limit
    std::vector<ReferenceItem> many;
    for (size_t i = 0; i < kMaxReferenceItems + 5; ++i) {
        ReferenceItem item = items[0];
        item.id = "id" + std::to_string(i) + "-1";
        many.push_back(item);
    }
    ReferenceImportReport second;
    addItems(set, many, second);
    CHECK(set.items.size() == kMaxReferenceItems);
    CHECK(second.added == kMaxReferenceItems - 1);
    CHECK(second.overLimit == 6);
}

TEST_CASE("the switches of an item can be changed by id", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    ReferenceImportReport report;
    ReferenceSet set;
    addItems(set, itemsFromMidiFile("bass.mid", file(list, 4), style, report), report);
    const auto id = set.items[0].id;

    CHECK(setItemActive(set, id, false));
    CHECK_FALSE(set.items[0].active);
    CHECK(activeCount(set) == 0);
    CHECK(setItemWeight(set, id, 9));
    CHECK(set.items[0].weight == 5);
    CHECK(setItemWeight(set, id, -2));
    CHECK(set.items[0].weight == 1);
    CHECK(setItemWeight(set, id, 4));
    CHECK(set.items[0].weight == 4);
    CHECK(setItemRole(set, id, TrackRole::Melody));
    CHECK(set.items[0].role == TrackRole::Melody);
    CHECK(countOfRole(set, TrackRole::Melody) == 1);
    CHECK(countOfRole(set, TrackRole::Bass) == 0);

    const auto before = set;
    CHECK_FALSE(setItemActive(set, "nope", true));
    CHECK_FALSE(setItemWeight(set, "nope", 2));
    CHECK_FALSE(setItemRole(set, "nope", TrackRole::Bass));
    CHECK_FALSE(removeItem(set, "nope"));
    CHECK(set == before);
    CHECK(removeItem(set, id));
    CHECK(set.items.empty());
}

TEST_CASE("a set survives writing and reading", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    addBass(list, 3, 2, 8, 12);
    ReferenceImportReport report;
    ReferenceSet set;
    set.styleId = "peak_time";
    addItems(set, itemsFromMidiFile("two.mid", file(list, 4), style, report), report);
    REQUIRE(set.items.size() == 2);
    setItemActive(set, set.items[1].id, false);
    setItemWeight(set, set.items[1].id, 5);
    setItemRole(set, set.items[1].id, TrackRole::Chords);

    const auto text = referenceSetToJson(set);
    size_t dropped = 99;
    const auto back = referenceSetFromJson(text, "peak_time", &dropped);
    CHECK(dropped == 0);
    CHECK(back == set);
    CHECK(back.items[1].role == TrackRole::Chords);
    CHECK_FALSE(back.items[1].active);
    CHECK(back.items[1].weight == 5);
}

TEST_CASE("a bad set file gives an empty set and a bad item is left out", "[reference-set]") {
    size_t dropped = 0;
    CHECK(referenceSetFromJson("", "x", &dropped).items.empty());
    CHECK(referenceSetFromJson("not json", "x").items.empty());
    CHECK(referenceSetFromJson("[]", "x").items.empty());
    CHECK(referenceSetFromJson("{\"items\":[]}", "x").items.empty()); // no version
    CHECK(referenceSetFromJson("{\"version\":0,\"items\":[]}", "x").items.empty());
    CHECK(referenceSetFromJson("{\"version\":1}", "x").items.empty());
    CHECK(referenceSetFromJson("{\"version\":1,\"items\":[]}", "keep").styleId == "keep");

    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    ReferenceImportReport report;
    ReferenceSet set;
    addItems(set, itemsFromMidiFile("a.mid", file(list, 4), style, report), report);
    auto json = nlohmann::json::parse(referenceSetToJson(set));
    auto good = json["items"][0];
    auto badRole = good;
    badRole["id"] = "r";
    badRole["role"] = "tuba";
    auto badNote = good;
    badNote["id"] = "n";
    badNote["notes"][0][1] = 200;
    auto noNotes = good;
    noNotes["id"] = "e";
    noNotes["notes"] = nlohmann::json::array();
    auto badAnalysis = good;
    badAnalysis["id"] = "a";
    badAnalysis["analysis"]["key"]["scale"] = "no_such";
    auto badBars = good;
    badBars["id"] = "b";
    badBars["bars"] = 3;
    auto twin = good; // the same id twice
    auto noId = good;
    noId["id"] = "";
    auto lateNote = good;
    lateNote["id"] = "l";
    lateNote["notes"][0][3] = 999999;
    auto shortNote = good;
    shortNote["id"] = "s";
    shortNote["notes"][0][4] = 0;
    auto weight = good;
    weight["id"] = "w";
    weight["weight"] = 77;
    json["items"] = nlohmann::json::array({good, badRole, badNote, noNotes, badAnalysis, badBars, twin, noId, lateNote,
                                           shortNote, weight, 5});
    const auto read = referenceSetFromJson(json.dump(), "peak_time", &dropped);
    CHECK(dropped == 10); // role, note pitch, no notes, analysis, bars, the twin, no id, late note, short note, a number
    REQUIRE(read.items.size() == 2);
    CHECK(read.items[0] == set.items[0]);
    CHECK(read.items[1].id == "w");
    CHECK(read.items[1].weight == 5); // clamped
    // a note that reaches over the end is cut there
    auto over = good;
    over["id"] = "o";
    over["notes"][0][4] = 99999999;
    json["items"] = nlohmann::json::array({over});
    const auto cut = referenceSetFromJson(json.dump(), "peak_time");
    REQUIRE(cut.items.size() == 1);
    CHECK(cut.items[0].notes[0].startTick + cut.items[0].notes[0].lengthTicks <= 4 * kTicksPerBar);
}

TEST_CASE("roles have names", "[reference-set]") {
    for (const auto role : {TrackRole::Drums, TrackRole::Chords, TrackRole::Bass, TrackRole::Melody}) {
        CHECK(trackRoleFromString(toString(role)) == role);
    }
    CHECK_FALSE(trackRoleFromString("tuba").has_value());
}

TEST_CASE("a track needs four notes inside the first 16 bars", "[reference-set]") {
    const auto style = shipped("peak_time");
    for (const uint32_t count : {3u, 4u}) {
        std::vector<PatternNote> list;
        for (uint32_t i = 0; i < count; ++i) {
            list.push_back({i * 960, 200, 1, 45, 100});
        }
        ReferenceImportReport report;
        const auto items = itemsFromMidiFile("few.mid", file(list, 1), style, report);
        CHECK(items.size() == (count == 4 ? 1u : 0u));
        CHECK(report.tooSmall == (count == 4 ? 0u : 1u));
    }
    // five notes, but two of them start after the 16th bar: three are left
    std::vector<PatternNote> late;
    for (uint32_t i = 0; i < 3; ++i) {
        late.push_back({i * 960, 200, 1, 45, 100});
    }
    late.push_back({17 * kTicksPerBar, 200, 1, 45, 100});
    late.push_back({18 * kTicksPerBar, 200, 1, 45, 100});
    ReferenceImportReport report;
    CHECK(itemsFromMidiFile("late.mid", file(late, 20), style, report).empty());
    CHECK(report.tooSmall == 1);
}

TEST_CASE("two channels give two named items", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    addBass(list, 2, 4, 4, 12);
    ReferenceImportReport report;
    const auto items = itemsFromMidiFile("pair.mid", file(list, 4), style, report);
    REQUIRE(items.size() == 2);
    CHECK(items[0].name == "pair.mid (1)");
    CHECK(items[1].name == "pair.mid (2)");
}

TEST_CASE("the hash ends with the length of the file", "[reference-set]") {
    const std::vector<uint8_t> a = {1, 2, 3};
    CHECK(fileHash(a.data(), a.size()).substr(16) == "00000003");
    const std::vector<uint8_t> big(0x1234, 7);
    CHECK(fileHash(big.data(), big.size()).substr(16) == "00001234");
}

TEST_CASE("a set file with out-of-range values or too many items is read as far as it is good", "[reference-set]") {
    const auto style = shipped("peak_time");
    std::vector<PatternNote> list;
    addBass(list, 1, 4);
    ReferenceImportReport report;
    ReferenceSet set;
    addItems(set, itemsFromMidiFile("a.mid", file(list, 4), style, report), report);
    auto json = nlohmann::json::parse(referenceSetToJson(set));
    const auto good = json["items"][0];

    auto silent = good;
    silent["id"] = "v";
    silent["notes"][0][2] = 0;
    auto channel0 = good;
    channel0["id"] = "c";
    channel0["notes"][0][0] = 0;
    auto badBars = good; // three bars: not a pattern length, although every note starts in the first bar
    badBars["id"] = "b";
    badBars["bars"] = 3;
    badBars["notes"] = nlohmann::json::array({good["notes"][0], good["notes"][1], good["notes"][2], good["notes"][3]});
    json["items"] = nlohmann::json::array({silent, channel0, badBars});
    size_t dropped = 0;
    CHECK(referenceSetFromJson(json.dump(), "x", &dropped).items.empty());
    CHECK(dropped == 3);

    nlohmann::json many = nlohmann::json::array();
    for (size_t i = 0; i < kMaxReferenceItems + 1; ++i) {
        auto item = good;
        item["id"] = "i" + std::to_string(i);
        many.push_back(item);
    }
    json["items"] = many;
    const auto read = referenceSetFromJson(json.dump(), "x", &dropped);
    CHECK(read.items.size() == kMaxReferenceItems);
    CHECK(dropped == 1);
}
