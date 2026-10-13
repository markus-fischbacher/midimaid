#include "core/MidiFile.h"
#include "core/ReferenceProfile.h"
#include "core/SetProfile.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
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
    set.items[0].analysis.key.scaleId = "phrygian";
    CHECK(std::none_of(profileOf(set).scales.begin(), profileOf(set).scales.end(),
                       [](const WeightedId& scale) { return scale.id == "phrygian"; }));
    set.items[0].role = TrackRole::Bass;
    CHECK(std::any_of(profileOf(set).scales.begin(), profileOf(set).scales.end(),
                      [](const WeightedId& scale) { return scale.id == "phrygian"; }));
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
