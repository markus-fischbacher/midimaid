#include "core/MidiFile.h"
#include "core/PatternGenerator.h"
#include "core/PatternJson.h"
#include "core/PatternValidation.h"
#include "core/ReferenceProfile.h"
#include "core/SetProfile.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace mm::core;

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

struct Hit {
    uint32_t step; ///< 16th step from the start (bar * 16 + step in the bar)
    int pitch;
};

/// An item from a list of hits (every note one 16th long), analysed like an import. The role is set by hand.
ReferenceItem itemOf(const std::string& id, const std::vector<Hit>& hits, uint32_t bars, TrackRole role = TrackRole::Bass,
                     int weight = kDefaultReferenceWeight) {
    std::vector<PatternNote> list;
    for (const auto& hit : hits) {
        list.push_back({hit.step * 240, 200, 1, static_cast<uint8_t>(hit.pitch), 100});
    }
    const auto bytes = writeMidiFile(PatternView{list.data(), list.size(), bars * kTicksPerBar}, MidiFileOptions{});
    ReferenceImportReport report;
    auto items = itemsFromMidiFile(id, bytes, shipped(), report);
    REQUIRE(items.size() == 1);
    items[0].id = id;
    items[0].role = role;
    items[0].roleConfidence = 1.0;
    items[0].weight = weight;
    return items[0];
}

/// A bar of notes on the steps 0, 4, 8, 12 with the given pitches, repeated over `bars` bars.
std::vector<Hit> quarters(uint32_t bars, const std::vector<int>& pitches) {
    std::vector<Hit> hits;
    for (uint32_t bar = 0; bar < bars; ++bar) {
        for (uint32_t i = 0; i < 4; ++i) {
            hits.push_back({bar * 16 + i * 4, pitches[i % pitches.size()]});
        }
    }
    return hits;
}

std::vector<Hit> sixteenths(uint32_t bars, int pitch = 45) {
    std::vector<Hit> hits;
    for (uint32_t i = 0; i < bars * 16; ++i) {
        hits.push_back({i, pitch});
    }
    return hits;
}

ReferenceSet setOf(std::vector<ReferenceItem> items) {
    ReferenceSet set;
    set.styleId = "peak_time";
    set.items = std::move(items);
    return set;
}

/// Five bass items with the same bar of quarters A A C G, so that cells and progressions exist.
ReferenceSet fiveBass(int shift = 0) {
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        items.push_back(itemOf("b" + std::to_string(i), quarters(4, {45 + shift, 45 + shift, 48 + shift, 43 + shift}), 4));
    }
    return setOf(std::move(items));
}

} // namespace

TEST_CASE("a set with fewer than five active items has no profile", "[set-profile]") {
    auto set = fiveBass();
    set.items[4].active = false;
    const auto profile = profileOf(set);
    CHECK_FALSE(profile.valid);
    CHECK(profile.activeItems == 4);
    CHECK(profile.cells.empty());
    CHECK(profile.scales.empty());
    CHECK(profile.progressions.empty());
    CHECK(profileOf(ReferenceSet{}).activeItems == 0);
    set.items[4].active = true;
    CHECK(profileOf(set).valid);
    CHECK(profileOf(set).activeItems == 5);
}

TEST_CASE("the profile counts what the lines have in common", "[set-profile]") {
    // three calm basses (4 notes per bar), one dense bass (16 per bar), one melody
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 3; ++i) {
        items.push_back(itemOf("calm" + std::to_string(i), quarters(4, {45}), 4));
    }
    items.push_back(itemOf("dense", sixteenths(4), 4, TrackRole::Bass, 2));
    items.push_back(itemOf("lead", quarters(4, {69, 71, 72, 74}), 4, TrackRole::Melody));
    const auto profile = profileOf(setOf(items));
    REQUIRE(profile.valid);
    CHECK(profile.bass.items == 4);
    CHECK(profile.melody.items == 1);
    // notes per bar: (3 * 4 * 3 + 2 * 16) / (3 * 3 + 2) = 68 / 11 = 6.18
    CHECK(profile.bass.notesPerBar100 == (3 * 3 * 400 + 2 * 1600) / 11);
    CHECK(profile.melody.notesPerBar100 == 400);
    // the classes follow the weights: calm 9 of 11, dense 2 of 11
    CHECK(profile.bass.classPermille[0] == 9 * 1000 / 11);
    CHECK(profile.bass.classPermille[1] == 0);
    CHECK(profile.bass.classPermille[2] == 2 * 1000 / 11);
    CHECK(profile.melody.classPermille[0] == 1000); // four notes per bar: calm for a melody
    // the steps: the calm basses use 0, 4, 8, 12, the dense one every step; 12 of 16 bars... 
    // (the share of bars, not weighted): 12 bars of calm and 4 of dense on steps 0 4 8 12, 4 of 16 on the others
    CHECK(profile.bass.stepPermille[0] == 1000);
    CHECK(profile.bass.stepPermille[4] == 1000);
    CHECK(profile.bass.stepPermille[1] == 4 * 1000 / 16);
    CHECK(profile.bass.stepPermille[15] == 4 * 1000 / 16);
    CHECK(profile.melody.stepPermille[0] == 1000);
    CHECK(profile.melody.stepPermille[1] == 0);
    CHECK(profile.bass.velocity == 100);
    CHECK(profile.bass.spanSemitones == 0);
}

TEST_CASE("drums count for nothing, chords for the scales only", "[set-profile]") {
    auto set = fiveBass();
    set.items[0].role = TrackRole::Drums;
    set.items[1].role = TrackRole::Chords;
    const auto profile = profileOf(set);
    REQUIRE(profile.valid);
    CHECK(profile.activeItems == 5); // active, but only the bass lines count below
    CHECK(profile.bass.items == 3);
    CHECK(profile.melody.items == 0);
    CHECK(profile.melody.notesPerBar100 == 0);
    // scales: the chord item counts, the drum item does not
    int total = 0;
    for (const auto& scale : profile.scales) {
        total += scale.weight;
    }
    auto without = set;
    without.items[1].active = false;
    int fewer = 0;
    for (const auto& scale : profileOf(setOf({without.items[0], without.items[2], without.items[3], without.items[4], without.items[0]})).scales) {
        fewer += scale.weight;
    }
    CHECK(total > 0);
    CHECK(fewer > 0);
    // the scale of a drum item is nowhere in the profile
    const auto hasPhrygian = [](const SetProfile& profile) {
        return std::any_of(profile.scales.begin(), profile.scales.end(), [](const WeightedId& scale) { return scale.id == "phrygian"; });
    };
    set.items[0].analysis.key.scaleId = "phrygian";
    CHECK_FALSE(hasPhrygian(profileOf(set)));
    set.items[0].role = TrackRole::Bass;
    CHECK(hasPhrygian(profileOf(set)));
}

TEST_CASE("the span is the mean of the lines weighted by the items", "[set-profile]") {
    std::vector<ReferenceItem> items;
    std::vector<Hit> wide; // 33 and 45 in turn: a span of an octave
    for (uint32_t i = 0; i < 8; ++i) {
        wide.push_back({i * 4, i % 2 == 0 ? 33 : 45});
    }
    items.push_back(itemOf("wide", wide, 2, TrackRole::Bass, 5));
    for (int i = 0; i < 4; ++i) {
        items.push_back(itemOf("flat" + std::to_string(i), quarters(2, {45}), 2, TrackRole::Bass, 1));
    }
    const auto profile = profileOf(setOf(items));
    REQUIRE(profile.valid);
    CHECK(profile.bass.spanSemitones == 5 * 12 / 9);
}

TEST_CASE("the scales are weighted by the items and sorted by id", "[set-profile]") {
    auto set = fiveBass();
    set.items[0].weight = 5;
    const auto profile = profileOf(set);
    REQUIRE_FALSE(profile.scales.empty());
    CHECK(std::is_sorted(profile.scales.begin(), profile.scales.end(),
                         [](const auto& a, const auto& b) { return a.id < b.id; }));
    int total = 0;
    for (const auto& scale : profile.scales) {
        CHECK(scale.weight > 0);
        total += scale.weight;
    }
    // a heavier item gives more: weights 5 3 3 3 3 against 3 3 3 3 3
    int plain = 0;
    for (const auto& scale : profileOf(fiveBass()).scales) {
        plain += scale.weight;
    }
    CHECK(total != plain);
}

TEST_CASE("tones outside the scale of a line are the chromatic share", "[set-profile]") {
    // A natural minor: A B C D E F G; the A# (46) is outside
    auto clean = fiveBass();
    CHECK(profileOf(clean).chromaticPermille == 0);
    auto set = fiveBass();
    for (auto& item : set.items) {
        item.analysis.key.root = 9;
        item.analysis.key.scaleId = "natural_minor";
    }
    CHECK(profileOf(set).chromaticPermille == 0);
    for (auto& item : set.items) {
        item.notes[1].pitch = 46; // one note of sixteen (four bars of four): outside A natural minor
    }
    CHECK(profileOf(set).chromaticPermille == 1000 / 16);
    // a key the scale of which is unknown counts no notes at all
    for (auto& item : set.items) {
        item.analysis.key.scaleId = "klingon";
    }
    CHECK(profileOf(set).chromaticPermille == 0);
}

TEST_CASE("a cell is a rhythm and the intervals, when two items hold it", "[set-profile][cells]") {
    const auto set = fiveBass();
    const auto cells = motifCellsOf(set, TrackRole::Bass);
    REQUIRE_FALSE(cells.empty());
    // the bars A A C G have the notes 45 45 48 43 each bar the same four, so the cell of one bar is steps 0 4 8 12
    const auto it = std::find_if(cells.begin(), cells.end(), [](const MotifCell& c) {
        return c.bars == 1 && c.steps == std::vector<uint8_t>{0, 4, 8, 12} && c.intervals == std::vector<int8_t>{0, 0, 3, -5};
    });
    REQUIRE(it != cells.end());
    CHECK(it->role == TrackRole::Bass);
    CHECK(it->count == 5 * kDefaultReferenceWeight);
    CHECK(motifCellsOf(set, TrackRole::Melody).empty());
    // a cell that only one item holds is not typical
    auto lone = fiveBass();
    lone.items[0] = itemOf("odd", {{0, 40}, {3, 43}, {7, 47}, {11, 41}}, 1);
    const auto without = motifCellsOf(lone, TrackRole::Bass);
    CHECK(std::none_of(without.begin(), without.end(), [](const MotifCell& c) {
        return c.steps == std::vector<uint8_t>{0, 3, 7, 11};
    }));
    // an inactive item does not count
    auto off = fiveBass();
    for (size_t i = 0; i < 4; ++i) {
        off.items[i].active = false;
    }
    CHECK(motifCellsOf(off, TrackRole::Bass).empty());
}

TEST_CASE("cells do not depend on the key and are the same for every item that has them", "[set-profile][cells]") {
    const auto a = motifCellsOf(fiveBass(0), TrackRole::Bass);
    const auto b = motifCellsOf(fiveBass(5), TrackRole::Bass);
    REQUIRE_FALSE(a.empty());
    CHECK(a == b);
    // five weights: the count is the sum of the weights of the items
    auto heavy = fiveBass();
    heavy.items[0].weight = 5;
    heavy.items[1].weight = 1;
    const auto cells = motifCellsOf(heavy, TrackRole::Bass);
    const auto top = std::find_if(cells.begin(), cells.end(), [](const MotifCell& c) { return c.bars == 1 && c.steps.size() == 4; });
    REQUIRE(top != cells.end());
    CHECK(top->count == 5 + 1 + 3 + 3 + 3);
}

TEST_CASE("two-bar cells are kept when the bars differ and dropped when one bar repeats", "[set-profile][cells]") {
    // two bars with different pitches (same rhythm): the one-bar cells are the same, the two-bar cell is its own
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        std::vector<Hit> hits;
        for (uint32_t bar = 0; bar < 2; ++bar) {
            for (uint32_t k = 0; k < 4; ++k) {
                const int offsets[2][4] = {{0, 1, 2, 3}, {0, 3, 1, 4}};
                hits.push_back({bar * 16 + k * 4, 45 + offsets[bar][k]});
            }
        }
        items.push_back(itemOf("t" + std::to_string(i), hits, 2));
    }
    const auto cells = motifCellsOf(setOf(items), TrackRole::Bass);
    const bool twoBar = std::any_of(cells.begin(), cells.end(), [](const MotifCell& c) { return c.bars == 2; });
    CHECK(twoBar);
    // the same bar twice: only the cell of one bar
    std::vector<ReferenceItem> same;
    for (int i = 0; i < 5; ++i) {
        same.push_back(itemOf("s" + std::to_string(i), quarters(2, {45, 47, 48, 50}), 2));
    }
    const auto onlyOne = motifCellsOf(setOf(same), TrackRole::Bass);
    REQUIRE_FALSE(onlyOne.empty());
    CHECK(std::none_of(onlyOne.begin(), onlyOne.end(), [](const MotifCell& c) { return c.bars == 2; }));
}

TEST_CASE("cells have three to sixteen notes, are sorted by count and are at most eight per role", "[set-profile][cells]") {
    // too few notes: two per bar
    std::vector<ReferenceItem> few;
    for (int i = 0; i < 5; ++i) {
        few.push_back(itemOf("f" + std::to_string(i), {{0, 45}, {8, 48}, {16, 45}, {24, 48}}, 2));
    }
    CHECK(motifCellsOf(setOf(few), TrackRole::Bass).empty());
    // a sixteenth bar has sixteen notes: the limit
    std::vector<ReferenceItem> dense;
    for (int i = 0; i < 5; ++i) {
        dense.push_back(itemOf("d" + std::to_string(i), sixteenths(1), 1));
    }
    const auto sixteen = motifCellsOf(setOf(dense), TrackRole::Bass);
    REQUIRE(sixteen.size() == 1);
    CHECK(sixteen[0].steps.size() == 16);
    // many different cells: the best eight, the highest count first
    std::vector<ReferenceItem> many;
    for (int i = 0; i < 5; ++i) {
        std::vector<Hit> hits;
        for (uint32_t bar = 0; bar < 12; ++bar) {
            for (uint32_t k = 0; k < 3 + bar % 3; ++k) {
                hits.push_back({bar * 16 + k * (bar + 1) % 15, 45 + static_cast<int>((k + bar) % 5)});
            }
        }
        std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.step < b.step; });
        hits.erase(std::unique(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.step == b.step; }), hits.end());
        many.push_back(itemOf("m" + std::to_string(i), hits, 12, TrackRole::Bass, 1 + i));
    }
    const auto cells = motifCellsOf(setOf(many), TrackRole::Bass);
    CHECK(cells.size() <= kMaxMotifCells);
    for (size_t i = 1; i < cells.size(); ++i) {
        CHECK(cells[i - 1].count >= cells[i].count);
    }
}

TEST_CASE("the profile holds the cells of both roles and the progressions of the set", "[set-profile]") {
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 3; ++i) {
        items.push_back(itemOf("b" + std::to_string(i), quarters(4, {45, 45, 48, 43}), 4));
    }
    for (int i = 0; i < 2; ++i) {
        items.push_back(itemOf("m" + std::to_string(i), quarters(4, {69, 72, 71, 67}), 4, TrackRole::Melody));
    }
    const auto set = setOf(items);
    const auto profile = profileOf(set);
    REQUIRE(profile.valid);
    CHECK(profile.progressions == personalProgressions(set));
    const auto bass = motifCellsOf(set, TrackRole::Bass);
    const auto melody = motifCellsOf(set, TrackRole::Melody);
    REQUIRE((!bass.empty() && !melody.empty()));
    std::vector<MotifCell> both = bass;
    both.insert(both.end(), melody.begin(), melody.end());
    CHECK(profile.cells == both); // bass first
    CHECK(profileOf(set) == profile); // a pure function
}

TEST_CASE("the facts of a profile name the progression, density, scale and cells", "[set-profile]") {
    const auto invalid = factsOf(SetProfile{});
    CHECK(invalid.progression.empty());
    CHECK(invalid.scaleId.empty());
    CHECK(invalid.cells == 0);
    const auto profile = profileOf(fiveBass());
    const auto facts = factsOf(profile);
    CHECK(facts.bassNotesPerBar100 == 400);
    CHECK(facts.melodyNotesPerBar100 == 0);
    CHECK(facts.scaleId == profile.scales[0].id); // only one scale here
    CHECK(facts.scalePercent == 100);
    CHECK(facts.cells == profile.cells.size());
    if (!profile.progressions.empty()) {
        CHECK_FALSE(facts.progression.empty());
    }
}

TEST_CASE("cells are sorted by how many items hold them", "[set-profile][cells]") {
    // every item has the bar X (quarters), two of five also a bar Y (steps 0 2 4 6)
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        std::vector<Hit> hits = quarters(1, {45, 45, 48, 43});
        if (i < 2) {
            for (uint32_t k = 0; k < 4; ++k) {
                hits.push_back({16 + k * 2, 45 + static_cast<int>(k % 2) * 7});
            }
        } else {
            for (uint32_t k = 0; k < 4; ++k) {
                hits.push_back({16 + k * 4, 40});
            }
        }
        items.push_back(itemOf("i" + std::to_string(i), hits, 2));
    }
    const auto cells = motifCellsOf(setOf(items), TrackRole::Bass);
    REQUIRE(cells.size() >= 2);
    for (size_t i = 1; i < cells.size(); ++i) {
        CHECK(cells[i - 1].count >= cells[i].count);
    }
    CHECK(cells.front().count == 5 * kDefaultReferenceWeight);
    CHECK(cells.front().steps == std::vector<uint8_t>{0, 4, 8, 12});
    CHECK(cells.back().count == 2 * kDefaultReferenceWeight);
}

TEST_CASE("tones at the same step make one note of the cell, the lowest", "[set-profile][cells]") {
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        std::vector<Hit> hits;
        for (uint32_t k = 0; k < 4; ++k) {
            hits.push_back({k * 4, 45});
            hits.push_back({k * 4, 52}); // a fifth on top, same step
        }
        items.push_back(itemOf("c" + std::to_string(i), hits, 1));
    }
    const auto cells = motifCellsOf(setOf(items), TrackRole::Bass);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].steps == std::vector<uint8_t>{0, 4, 8, 12});
    CHECK(cells[0].intervals == std::vector<int8_t>{0, 0, 0, 0}); // the line is the lowest tone
}

TEST_CASE("intervals of a cell are cut to two octaves", "[set-profile][cells]") {
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        // 33 to 63 is thirty semitones up, then fifteen down, then back down twenty
        items.push_back(itemOf("j" + std::to_string(i), {{0, 33}, {4, 63}, {8, 48}, {12, 28}}, 1));
    }
    const auto cells = motifCellsOf(setOf(items), TrackRole::Bass);
    REQUIRE(cells.size() == 1);
    CHECK(cells[0].intervals == std::vector<int8_t>{0, 24, -15, -20});
}

// ---- the snapshot and the effect on the style (D-241) ------------------------------------------------------------------

namespace {

/// A valid profile made by hand: the scales, the density classes (calm, neutral, dense in permille) of both roles and the
/// chromatic share.
SetProfile handProfile(std::vector<WeightedId> scales, std::array<int, 3> bassClasses, std::array<int, 3> melodyClasses,
                       int chromaticPermille = 0) {
    SetProfile profile = profileOf(fiveBass());
    REQUIRE(profile.valid);
    profile.scales = std::move(scales);
    profile.bass.classPermille = bassClasses;
    profile.melody.items = 3;
    profile.melody.classPermille = melodyClasses;
    profile.chromaticPermille = chromaticPermille;
    return profile;
}

int weightOf(const std::vector<WeightedId>& weights, const std::string& id) {
    const auto it = std::find_if(weights.begin(), weights.end(), [&](const WeightedId& w) { return w.id == id; });
    return it == weights.end() ? -1 : it->weight;
}

} // namespace

TEST_CASE("the profile is kept as text and read back", "[set-profile][snapshot]") {
    auto profile = profileOf(fiveBass());
    REQUIRE(profile.valid);
    REQUIRE_FALSE(profile.cells.empty());
    REQUIRE_FALSE(profile.progressions.empty());
    const auto text = setProfileToJson(profile);
    REQUIRE_FALSE(text.empty());
    const auto back = setProfileFromJson(text);
    REQUIRE(back.has_value());
    CHECK(*back == profile);
    CHECK(setProfileToJson(*back) == text); // stable
    CHECK(text.find('\n') == std::string::npos); // compact
    // an invalid profile has no text
    CHECK(setProfileToJson(SetProfile{}).empty());
    // a profile with cells of both roles and several progressions
    profile.cells.push_back({TrackRole::Melody, 2, {0, 4, 20}, {0, 2, -3}, 7});
    profile.progressions.push_back({{Chord{0, ChordQuality::Minor}, Chord{8, ChordQuality::Major}}, 11});
    CHECK(setProfileFromJson(setProfileToJson(profile)) == profile);
}

TEST_CASE("text that is no profile is refused", "[set-profile][snapshot]") {
    const auto good = nlohmann::json::parse(setProfileToJson(profileOf(fiveBass())));
    for (const char* text : {"", "not json", "[]", "{}", "null", "42", "\"x\""}) {
        INFO(text);
        CHECK_FALSE(setProfileFromJson(text).has_value());
    }
    const auto broken = [&](const std::function<void(nlohmann::json&)>& change) {
        auto copy = good;
        change(copy);
        return setProfileFromJson(copy.dump()).has_value();
    };
    CHECK(broken([](nlohmann::json&) {})); // the good one
    CHECK_FALSE(broken([](nlohmann::json& j) { j.erase("scales"); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j.erase("cells"); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j.erase("progressions"); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["items"] = 4; })); // fewer than five items: no profile
    CHECK_FALSE(broken([](nlohmann::json& j) { j["items"] = "5"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["chromatic"] = 1001; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["bass"]["steps"][3] = 1001; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["bass"]["steps"].erase(j["bass"]["steps"].begin()); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["bass"]["classes"][0] = -1; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["melody"] = 5; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["scales"][0][0] = "klingon"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["scales"][0][1] = 0; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["scales"].push_back(j["scales"][0]); })); // a scale twice
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["role"] = "drums"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["bars"] = 3; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["count"] = 0; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["steps"] = nlohmann::json::array({0, 4}); })); // fewer than 3 notes
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["steps"][1] = 0; }));  // not ascending
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["steps"][3] = 16; })); // outside the bar
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["intervals"][1] = 25; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["intervals"][0] = 2; })); // the first is 0
    CHECK_FALSE(broken([](nlohmann::json& j) { j["cells"][0]["intervals"].push_back(0); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["progressions"][0]["weight"] = 0; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["progressions"][0]["chords"] = nlohmann::json::array({"i"}); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["progressions"][0]["chords"][0] = "H#9"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["progressions"][0]["chords"][0] = 7; }));
}

TEST_CASE("a profile without effect changes nothing", "[set-profile][personal]") {
    const auto style = shipped();
    const auto profile = handProfile({{"phrygian", 100}}, {1000, 0, 0}, {0, 0, 1000}, 400);
    CHECK(personalizeStyle(style, profile, 0) == style);
    CHECK(personalizeStyle(style, SetProfile{}, 100) == style);
    auto invalid = profile;
    invalid.valid = false;
    CHECK(personalizeStyle(style, invalid, 100) == style);
}

TEST_CASE("the scales of the style move towards the set, by at most half", "[set-profile][personal]") {
    const auto style = shipped(); // natural_minor 3, phrygian 2, dorian 2, minor_pentatonic 1
    const auto profile = handProfile({{"phrygian", 100}}, {0, 1000, 0}, {0, 1000, 0});
    const auto full = personalizeStyle(style, profile, 100);
    // the set has only phrygian: its share is far above the style's (25 % to 100 %), the others fall to nothing
    REQUIRE(full.scales.size() == style.scales.size());
    CHECK(weightOf(full.scales, "phrygian") == 2 * kWeightResolution * 3 / 2);        // +50 %
    CHECK(weightOf(full.scales, "natural_minor") == 3 * kWeightResolution / 2);        // -50 %
    CHECK(weightOf(full.scales, "dorian") == 2 * kWeightResolution / 2);
    CHECK(weightOf(full.scales, "minor_pentatonic") == 1 * kWeightResolution / 2);
    // half the way at 50: at most 25 %
    const auto half = personalizeStyle(style, profile, 50);
    CHECK(weightOf(half.scales, "phrygian") == 2 * kWeightResolution * 5 / 4);
    CHECK(weightOf(half.scales, "natural_minor") == 3 * kWeightResolution * 3 / 4);
    // the ids and the order stay
    for (size_t i = 0; i < style.scales.size(); ++i) {
        CHECK(full.scales[i].id == style.scales[i].id);
    }
    // a set that is like the style changes the ratios not at all
    const auto alike = handProfile({{"natural_minor", 3}, {"phrygian", 2}, {"dorian", 2}, {"minor_pentatonic", 1}}, {0, 1000, 0}, {0, 1000, 0});
    const auto same = personalizeStyle(style, alike, 100);
    for (size_t i = 0; i < style.scales.size(); ++i) {
        CHECK(same.scales[i].weight == style.scales[i].weight * kWeightResolution);
    }
    // scales of the set that the style does not have are of no interest; none in common: unchanged
    const auto foreign = handProfile({{"locrian", 100}}, {0, 1000, 0}, {0, 1000, 0});
    const auto untouched = personalizeStyle(style, foreign, 100);
    for (size_t i = 0; i < style.scales.size(); ++i) {
        CHECK(untouched.scales[i].weight == style.scales[i].weight);
    }
    // no weight falls to 0
    for (const auto& scale : full.scales) {
        CHECK(scale.weight >= 1);
    }
}

TEST_CASE("the archetypes move by the density classes of the set", "[set-profile][personal]") {
    const auto style = shipped(); // bass: rolling16 4 (dense), offbeat 3 and gallop 3 (neutral)
    // a set of only dense bass lines: the dense archetype gains, the neutral ones lose
    const auto dense = handProfile({{"natural_minor", 1}}, {0, 0, 1000}, {0, 1000, 0});
    const auto moved = personalizeStyle(style, dense, 100);
    CHECK(weightOf(moved.bass.archetypes, "rolling16") == 4 * kWeightResolution * 3 / 2);
    CHECK(weightOf(moved.bass.archetypes, "offbeat") == 3 * kWeightResolution / 2);
    CHECK(weightOf(moved.bass.archetypes, "gallop") == 3 * kWeightResolution / 2);
    // melody: a set of only neutral lines: hypnotic_motif, stabs and polymeter_seq (neutral, 10 of 13 in the style: 77 %)
    // gain 100 / 77 = 130 %, arp and acid_siren (dense, 3 of 13) lose half
    CHECK(weightOf(moved.melody.archetypes, "hypnotic_motif") == 4 * kWeightResolution * 13 / 10);
    CHECK(weightOf(moved.melody.archetypes, "stabs") == 3 * kWeightResolution * 13 / 10);
    CHECK(weightOf(moved.melody.archetypes, "arp") == 2 * kWeightResolution / 2);
    CHECK(weightOf(moved.melody.archetypes, "acid_siren") == 1 * kWeightResolution / 2);
    // the half way
    const auto half = personalizeStyle(style, dense, 50);
    CHECK(weightOf(half.bass.archetypes, "rolling16") == 4 * kWeightResolution * 5 / 4);
    // a role with no line in the set stays as it is
    auto noMelody = dense;
    noMelody.melody = RoleStats{};
    const auto kept = personalizeStyle(style, noMelody, 100);
    CHECK(kept.melody.archetypes == style.melody.archetypes);
    CHECK(kept.bass.archetypes != style.bass.archetypes);
    // the ids, their order and the rest of the profile stay
    CHECK(moved.bass.archetypes.size() == style.bass.archetypes.size());
    CHECK(moved.bass.archetypes[0].id == "rolling16");
    CHECK(moved.harmony == style.harmony);
    CHECK(moved.bass.range == style.bass.range);
}

TEST_CASE("the chromatic default moves towards the set, by at most half of itself", "[set-profile][personal]") {
    auto style = shipped();
    style.chromaticDefaultPercent = 20;
    // a set with 80 % chromatic: the way is long, the limit is half of 20: 10
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 800), 100).chromaticDefaultPercent == 30);
    // half the way at 50 %, cut by the limit: 60 * 50 / 100 = 30 -> 10
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 800), 50).chromaticDefaultPercent == 30);
    // a set with 30 %: 10 points of the way, at 50 % five
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 300), 100).chromaticDefaultPercent == 30);
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 300), 50).chromaticDefaultPercent == 25);
    // downwards
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 0), 100).chromaticDefaultPercent == 10);
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 150), 100).chromaticDefaultPercent == 15);
    // the share of the set is rounded to whole percent: 45.5 % is 46
    auto rounded = style;
    rounded.chromaticDefaultPercent = 40;
    CHECK(personalizeStyle(rounded, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 455), 100).chromaticDefaultPercent == 46);
    CHECK(personalizeStyle(rounded, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 454), 100).chromaticDefaultPercent == 45);
    // a style without chromatics can still move, by the minimum of five points
    style.chromaticDefaultPercent = 0;
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 900), 100).chromaticDefaultPercent == 5);
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 20), 100).chromaticDefaultPercent == 2);
    // never outside 0 to 100
    style.chromaticDefaultPercent = 100;
    CHECK(personalizeStyle(style, handProfile({{"natural_minor", 1}}, {0, 1000, 0}, {0, 1000, 0}, 1000), 100).chromaticDefaultPercent == 100);
}

TEST_CASE("the personal progressions are scaled by the setting", "[set-profile][personal]") {
    const std::vector<WeightedProgression> list{{{Chord{0, ChordQuality::Minor}, Chord{8, ChordQuality::Major}}, 10},
                                                {{Chord{0, ChordQuality::Minor}, Chord{10, ChordQuality::Major}}, 1}};
    CHECK(scaledProgressions(list, 50) == list);
    const auto doubled = scaledProgressions(list, 100);
    REQUIRE(doubled.size() == 2);
    CHECK(doubled[0].weight == 20);
    CHECK(doubled[1].weight == 2);
    CHECK(scaledProgressions(list, 0).empty());
    const auto low = scaledProgressions(list, 4); // 10 * 4 / 50 = 0: dropped; 1 * 4 / 50 = 0: dropped
    CHECK(low.empty());
    const auto some = scaledProgressions(list, 10);
    REQUIRE(some.size() == 1);
    CHECK(some[0].weight == 2);
    CHECK(scaledProgressions(list, 500) == scaledProgressions(list, 100)); // cut to 100
    CHECK(scaledProgressions({}, 50).empty());
}

// ---- the profile in a generation ------------------------------------------------------------------------------------------

namespace {

GenerationRequest requestWith(const std::optional<SetProfile>& profile, int personalPct = 50, uint64_t seed = 31) {
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    request.settings.personalPct = personalPct;
    request.profile = profile;
    return request;
}

/// A profile that only wants phrygian, dense bass lines and a lot of chromatics.
SetProfile phrygianProfile() {
    return handProfile({{"phrygian", 100}}, {0, 0, 1000}, {0, 0, 1000}, 600);
}

} // namespace

TEST_CASE("a pattern keeps the profile it was made with and the setting", "[set-profile][generation]") {
    const auto style = shipped();
    const auto profile = phrygianProfile();
    const auto result = generatePattern(style, requestWith(profile, 70));
    REQUIRE(result.success);
    CHECK(result.pattern.info.profile == setProfileToJson(profile));
    CHECK(result.pattern.info.personalPct == 70);
    CHECK(validatePattern(result.pattern).empty());
    // without a profile (a set that is too small) the pattern has none, as before
    const auto plain = generatePattern(style, requestWith(std::nullopt));
    REQUIRE(plain.success);
    CHECK(plain.pattern.info.profile.empty());
    CHECK(plain.pattern.info.personalPct == 50);
    // an invalid profile is as good as none
    const auto invalid = generatePattern(style, requestWith(SetProfile{}));
    REQUIRE(invalid.success);
    CHECK(invalid.pattern.info.profile.empty());
    CHECK(invalid.pattern == plain.pattern);
    // a candidate carries it too (the winner is one of them) and a replay of the winner is the pattern
    const auto replay = replayWinner(style, requestWith(profile, 70), result.pattern.info.winnerSeed);
    CHECK(replay.voices == result.pattern.voices);
    CHECK(replay.info.profile == result.pattern.info.profile);
}

TEST_CASE("the profile changes what is generated, the same request the same pattern", "[set-profile][generation]") {
    const auto style = shipped();
    const auto profile = phrygianProfile();
    const auto a = generatePattern(style, requestWith(profile, 100));
    const auto b = generatePattern(style, requestWith(profile, 100));
    REQUIRE((a.success && b.success));
    CHECK(a.pattern == b.pattern); // deterministic
    // over several seeds the personal results are not all the plain ones
    int different = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        const auto personal = generatePattern(style, requestWith(profile, 100, seed));
        const auto plain = generatePattern(style, requestWith(std::nullopt, 100, seed));
        REQUIRE((personal.success && plain.success));
        different += personal.pattern.voices != plain.pattern.voices ? 1 : 0;
    }
    CHECK(different >= 6);
    // phrygian is what the set wants: it comes up more often than in plain generation
    int personalPhrygian = 0;
    int plainPhrygian = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        auto personal = requestWith(profile, 100, seed);
        auto plain = requestWith(std::nullopt, 100, seed);
        const auto p = generatePattern(style, personal);
        const auto q = generatePattern(style, plain);
        personalPhrygian += p.success && p.pattern.context.scaleId == "phrygian" ? 1 : 0;
        plainPhrygian += q.success && q.pattern.context.scaleId == "phrygian" ? 1 : 0;
    }
    CHECK(personalPhrygian > plainPhrygian);
}

TEST_CASE("with the setting at 0 the set does nothing but is remembered", "[set-profile][generation]") {
    const auto style = shipped();
    const auto profile = phrygianProfile();
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        const auto zero = generatePattern(style, requestWith(profile, 0, seed));
        auto plainRequest = requestWith(std::nullopt, 0, seed);
        const auto plain = generatePattern(style, plainRequest);
        REQUIRE((zero.success && plain.success));
        CHECK(zero.pattern.voices == plain.pattern.voices);
        CHECK(zero.pattern.context == plain.pattern.context);
        CHECK(zero.pattern.info.personalPct == 0);
        CHECK_FALSE(zero.pattern.info.profile.empty());
    }
}

TEST_CASE("the personal progressions join at 50 and are scaled by the setting", "[set-profile][generation]") {
    const auto style = shipped();
    // a profile with one progression the style does not have, very heavy
    auto profile = phrygianProfile();
    profile.progressions = {{{Chord{0, ChordQuality::Minor}, Chord{3, ChordQuality::Diminished}}, 100000}};
    int own100 = 0;
    int own0 = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        const auto high = generatePattern(style, requestWith(profile, 100, seed));
        const auto none = generatePattern(style, requestWith(profile, 0, seed));
        REQUIRE((high.success && none.success));
        const auto ownChords = [](const Pattern& pattern) {
            for (const auto& event : pattern.context.progression) {
                if (event.chord.quality == ChordQuality::Diminished && event.chord.rootOffset == 3) {
                    return true;
                }
            }
            return false;
        };
        own100 += ownChords(high.pattern) ? 1 : 0;
        own0 += ownChords(none.pattern) ? 1 : 0;
    }
    CHECK(own100 > 0);
    CHECK(own0 == 0); // at 0 the set's progressions are dropped
}

TEST_CASE("a variation works with the profile of the pattern, not with the set of today", "[set-profile][generation]") {
    const auto style = shipped();
    const auto profile = phrygianProfile();
    const auto made = generatePattern(style, requestWith(profile, 100));
    REQUIRE(made.success);
    // the request of a variation carries no profile: the snapshot of the pattern is used
    auto plainRequest = requestWith(std::nullopt, 20); // not the setting of the pattern: that one is in its snapshot
    Pattern byFlag = made.pattern;
    Pattern bySnapshot = made.pattern;
    REQUIRE(regenerateVoice(bySnapshot, 0, style, plainRequest, 777));
    auto withProfile = requestWith(profile, 100);
    const auto withProfile0 = [&] { return requestWith(profile, 100); };
    REQUIRE(regenerateVoice(byFlag, 0, style, withProfile, 777));
    CHECK(bySnapshot.voices == byFlag.voices);
    CHECK(bySnapshot.info.profile == made.pattern.info.profile); // the snapshot stays
    // more variations: the setting of the snapshot (100), not that of the request (20), decides every time
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        Pattern a = made.pattern;
        Pattern b = made.pattern;
        for (size_t voice = 0; voice < 2; ++voice) {
            REQUIRE(regenerateVoice(a, voice, style, plainRequest, seed * 7919));
            REQUIRE(regenerateVoice(b, voice, style, withProfile0(), seed * 7919));
        }
        CHECK(a.voices == b.voices);
    }
    // a pattern without a snapshot is regenerated with the style as it is
    Pattern old = made.pattern;
    old.info.profile.clear();
    Pattern oldPlain = old;
    REQUIRE(regenerateVoice(old, 0, style, plainRequest, 777));
    REQUIRE(regenerateVoice(oldPlain, 0, style, requestWith(std::nullopt, 100), 777));
    CHECK(old.voices == oldPlain.voices);
    // phrases too
    Pattern phrased = made.pattern;
    Pattern phrasedByRequest = made.pattern;
    if (!phrased.phrases.empty()) {
        const bool a = regeneratePhrase(phrased, 0, style, plainRequest, 5);
        const bool b = regeneratePhrase(phrasedByRequest, 0, style, withProfile, 5);
        CHECK(a == b);
        CHECK(phrased.voices == phrasedByRequest.voices);
    }
}

TEST_CASE("the snapshot is saved with the pattern and checked when it is read", "[set-profile][snapshot]") {
    const auto style = shipped();
    const auto made = generatePattern(style, requestWith(phrygianProfile(), 65));
    REQUIRE(made.success);
    const auto text = patternToString(made.pattern);
    const auto back = loadPattern(text);
    REQUIRE(back.ok());
    CHECK(back.pattern->info.profile == made.pattern.info.profile);
    CHECK(back.pattern->info.personalPct == 65);
    CHECK(*back.pattern == made.pattern);
    // a pattern without a snapshot has no such field (patterns of before stay as they were)
    const auto plain = generatePattern(style, requestWith(std::nullopt));
    REQUIRE(plain.success);
    const auto plainJson = nlohmann::json::parse(patternToString(plain.pattern));
    CHECK_FALSE(plainJson["info"].contains("profile"));
    CHECK_FALSE(plainJson["info"].contains("personalPct"));
    // a snapshot that is no profile is refused, a setting above 100 too
    auto broken = nlohmann::json::parse(text);
    broken["info"]["profile"] = nlohmann::json::object({{"items", 2}});
    CHECK_FALSE(loadPatternJson(broken).ok());
    broken = nlohmann::json::parse(text);
    broken["info"]["personalPct"] = 101;
    CHECK_FALSE(loadPatternJson(broken).ok());
    broken = nlohmann::json::parse(text);
    broken["info"]["profile"] = "text";
    CHECK_FALSE(loadPatternJson(broken).ok());
    // the pattern itself validates
    auto direct = made.pattern;
    direct.info.profile = "{}";
    CHECK_FALSE(validatePattern(direct).empty());
    direct.info.profile = made.pattern.info.profile;
    direct.info.personalPct = 101;
    CHECK_FALSE(validatePattern(direct).empty());
}

TEST_CASE("generation around locks moves the style towards the profile and keeps the snapshot too", "[set-profile][generation]") {
    const auto style = shipped();
    const auto profile = phrygianProfile();
    const auto base = generatePattern(style, requestWith(std::nullopt, 100));
    REQUIRE(base.success);
    Pattern current = base.pattern;
    current.voices[0].lock = LockFlags{true, true, true};
    const auto result = generatePatternAroundLocks(style, requestWith(profile, 90), current);
    REQUIRE(result.success);
    CHECK(result.pattern.info.profile == setProfileToJson(profile));
    CHECK(result.pattern.info.personalPct == 90);
    // the same request without a profile leaves no snapshot
    const auto plain = generatePatternAroundLocks(style, requestWith(std::nullopt, 90), current);
    REQUIRE(plain.success);
    CHECK(plain.pattern.info.profile.empty());
}
