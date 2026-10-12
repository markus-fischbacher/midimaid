#include "core/Archetype.h"
#include "core/Pattern.h"
#include "core/PatternGenerator.h"
#include "core/Random.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>
#include <sstream>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;

namespace {

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

/// The sequence without the other voices: a melody over one chord, as `generateVoice` makes it.
std::vector<Note> sequence(uint32_t bars, uint64_t seed, int energy = 50, int chromaticPct = -1) {
    StyleProfile style = loadShipped("peak_time");
    if (chromaticPct >= 0) {
        style.chromaticDefaultPercent = chromaticPct;
    }
    Pattern p = makeEmptyPattern(bars, "peak_time");
    Pcg32 rng = Pcg32::fromSeed(seed);
    REQUIRE(generateVoice(p, 1, "polymeter_seq", style, ArchetypeSettings{energy, 40}, rng));
    return p.voices[1].notes;
}

std::vector<bool> onsets(const std::vector<Note>& notes, uint32_t bars) {
    std::vector<bool> steps(bars * 16, false);
    for (const Note& note : notes) {
        REQUIRE(note.startTick % 240 == 0);
        steps[note.startTick / 240] = true;
    }
    return steps;
}

bool periodic(const std::vector<bool>& steps, size_t period) {
    for (size_t s = 0; s + period < steps.size(); ++s) {
        if (steps[s] != steps[s + period]) {
            return false;
        }
    }
    return true;
}

/// The smallest cycle of the allowed lengths the onsets repeat with, 0 if none.
size_t cycleOf(const std::vector<bool>& steps) {
    for (const size_t cycle : {3, 5, 6, 7, 9, 10, 14}) {
        if (periodic(steps, cycle)) {
            return cycle;
        }
    }
    return 0;
}

} // namespace

TEST_CASE("polymeter_seq is a melody archetype of the style", "[polymeter]") {
    const Archetype* archetype = findArchetype("polymeter_seq");
    REQUIRE(archetype != nullptr);
    CHECK(archetype->role == VoiceRole::Melody);
    CHECK_FALSE(archetype->ignoresKick);
    bool listed = false;
    for (const WeightedId& entry : loadShipped("peak_time").melody.archetypes) {
        listed = listed || entry.id == "polymeter_seq";
    }
    CHECK(listed);
}

TEST_CASE("the hits repeat in a cell of 3, 5, 6, 7, 9, 10 or 14 sixteenths, across the bar lines", "[polymeter]") {
    std::set<size_t> cycles;
    int barRepeat = 0;
    int runs = 0;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        const auto notes = sequence(4, seed);
        REQUIRE_FALSE(notes.empty());
        const auto steps = onsets(notes, 4);
        const size_t cycle = cycleOf(steps);
        INFO("seed " << seed);
        CHECK(cycle != 0);
        cycles.insert(cycle);
        ++runs;
        barRepeat += periodic(steps, 16) ? 1 : 0;
    }
    // the cells that are shorter than their hits repeat themselves are found by their smallest period: 3, 5, 6, 7
    for (const size_t cycle : {3, 5, 7}) {
        CHECK(cycles.count(cycle) == 1);
    }
    CHECK(barRepeat * 100 < runs * 40); // most sequences do not repeat bar for bar
}

TEST_CASE("the sequence starts with the pattern and the cell runs on across a longer pattern", "[polymeter]") {
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const auto two = sequence(2, seed);
        const auto four = sequence(4, seed);
        REQUIRE_FALSE(two.empty());
        INFO("seed " << seed);
        CHECK(four.front().startTick == 0);
        CHECK(four.front().accent); // the cell start is accented
        // the first two bars are the same: the cell starts at tick 0 of the pattern, the pattern length is the only
        // difference (the lengths of the last notes are cut at the pattern end)
        size_t i = 0;
        for (const Note& note : two) {
            REQUIRE(i < four.size());
            CHECK(note.startTick == four[i].startTick); // (the chromatic pass may change a tone by a semitone)
            ++i;
        }
    }
}

TEST_CASE("2 to 5 tones run through the hits", "[polymeter]") {
    int atLeastTwo = 0;
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        std::set<int> pitches;
        for (const Note& note : sequence(4, seed, 50, 0)) { // without the chromatic pass
            pitches.insert(note.pitch);
        }
        INFO("seed " << seed);
        CHECK(pitches.size() <= 5);
        atLeastTwo += pitches.size() >= 2 ? 1 : 0;
    }
    CHECK(atLeastTwo >= 80);
}

TEST_CASE("the density follows the energy and the sequence is deterministic", "[polymeter]") {
    size_t low = 0;
    size_t high = 0;
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        low += sequence(4, seed, 0).size();
        high += sequence(4, seed, 100).size();
        CHECK(sequence(4, seed, 50) == sequence(4, seed, 50));
    }
    CHECK(high > low);
}

TEST_CASE("a generated pattern can carry the sequence and keeps all the rules", "[polymeter][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        GenerationRequest request;
        request.lengthBars = 4;
        request.seed = seed;
        request.melodyArchetype = "polymeter_seq";
        const Pattern p = generateCandidate(style, request, seed);
        INFO("seed " << seed);
        CHECK(p.voices[1].archetypeId == "polymeter_seq");
        CHECK_FALSE(p.voices[1].notes.empty());
    }
}

TEST_CASE("the tones follow the chord of the moment", "[polymeter]") {
    const StyleProfile style = loadShipped("peak_time");
    int moved = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern p = makeEmptyPattern(2, "peak_time");
        p.context.progression.clear();
        ChordEvent first;
        first.chord = {0, ChordQuality::Minor};
        first.startHalfBar = 0;
        first.lengthHalfBars = 2;
        ChordEvent second;
        second.chord = {8, ChordQuality::Major};
        second.startHalfBar = 2;
        second.lengthHalfBars = 2;
        p.context.progression = {first, second};
        Pcg32 rng = Pcg32::fromSeed(seed);
        REQUIRE(generateVoice(p, 1, "polymeter_seq", style, ArchetypeSettings{50, 40}, rng));
        std::set<int> a;
        std::set<int> b;
        for (const Note& note : p.voices[1].notes) {
            (note.startTick < kTicksPerBar ? a : b).insert(note.pitch);
        }
        moved += a != b ? 1 : 0;
    }
    CHECK(moved >= 35);
}

TEST_CASE("the hits of a cell are spread evenly", "[polymeter]") {
    int checked = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        const auto steps = onsets(sequence(4, seed, 50, 0), 4);
        const size_t cycle = cycleOf(steps);
        REQUIRE(cycle != 0);
        std::vector<size_t> hits;
        for (size_t s = 0; s < cycle; ++s) {
            if (steps[s]) {
                hits.push_back(s);
            }
        }
        if (hits.size() < 2) {
            continue;
        }
        size_t smallest = cycle;
        size_t largest = 0;
        for (size_t i = 0; i < hits.size(); ++i) {
            const size_t next = i + 1 < hits.size() ? hits[i + 1] : hits[0] + cycle;
            smallest = std::min(smallest, next - hits[i]);
            largest = std::max(largest, next - hits[i]);
        }
        INFO("seed " << seed);
        CHECK(largest - smallest <= 1);
        ++checked;
    }
    CHECK(checked > 150);
}

TEST_CASE("the tones run through the hits without a restart at the bar lines", "[polymeter]") {
    int checked = 0;
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        const auto notes = sequence(4, seed, 50, 0);
        bool found = false;
        for (size_t period = 2; period <= 5 && !found; ++period) {
            bool all = notes.size() > period;
            for (size_t k = 0; k + period < notes.size() && all; ++k) {
                all = notes[k].pitch == notes[k + period].pitch;
            }
            found = all;
        }
        INFO("seed " << seed);
        CHECK(found);
        ++checked;
    }
    CHECK(checked == 100);
}

TEST_CASE("a note lasts a 16th when the next hit follows at once, else a little longer", "[polymeter]") {
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        const auto notes = sequence(4, seed, 50, 0);
        INFO("seed " << seed);
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
            const uint32_t gap = (notes[i + 1].startTick - notes[i].startTick) / 240;
            CHECK(notes[i].lengthTicks == (gap == 1 ? 180u : 300u));
        }
    }
}
