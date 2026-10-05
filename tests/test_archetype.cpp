#include "PatternFixtures.h"
#include "core/Archetype.h"
#include "core/KickGrid.h"
#include "core/Theory.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;
using namespace mm::fixtures;

namespace {

constexpr uint32_t kStep = 240;
constexpr uint32_t kHalfBar = kTicksPerBar / 2;

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    INFO(name << ": " << result.error);
    REQUIRE(result.ok());
    return *result.profile;
}

const std::vector<std::string> kStyles{"peak_time", "melodic_techno", "hard_industrial"};

/// A pattern over A natural minor (or the first scale of the style) with the given kick grid.
Pattern makePattern(uint32_t bars, const std::string& kickGrid = "4otf") {
    Pattern p = makeEmptyPattern(bars, "test");
    p.kickGridId = kickGrid;
    return p;
}

struct ChordSpan {
    uint8_t rootOffset;
    ChordQuality quality;
    uint32_t halfBars;
};

void setProgression(Pattern& p, const std::vector<ChordSpan>& spans) {
    p.context.progression.clear();
    uint32_t position = 0;
    for (const ChordSpan& span : spans) {
        ChordEvent event;
        event.chord = {span.rootOffset, span.quality};
        event.startHalfBar = position;
        event.lengthHalfBars = span.halfBars;
        p.context.progression.push_back(event);
        position += span.halfBars;
    }
    REQUIRE(position == p.lengthBars * 2);
}

/// Generates the bass of `p` (voice 0) and returns its notes.
std::vector<Note> bassOf(Pattern& p, const std::string& id, const StyleProfile& style, uint64_t seed, int energy = 50,
                         int creativity = 40) {
    Pcg32 rng = Pcg32::fromSeed(seed);
    REQUIRE(generateVoice(p, 0, id, style, ArchetypeSettings{energy, creativity}, rng));
    return p.voices[0].notes;
}

std::vector<uint32_t> startsInBar(const std::vector<Note>& notes, uint32_t bar) {
    std::vector<uint32_t> steps;
    for (const Note& note : notes) {
        if (note.startTick / kTicksPerBar == bar) {
            REQUIRE(note.startTick % kStep == 0);
            steps.push_back((note.startTick % kTicksPerBar) / kStep);
        }
    }
    return steps;
}

using Steps = std::vector<uint32_t>;

bool isKickTick(const std::vector<uint32_t>& kicks, uint32_t tick) {
    return std::binary_search(kicks.begin(), kicks.end(), tick);
}

} // namespace

TEST_CASE("the archetype table lists the nine bass archetypes", "[archetype]") {
    const std::vector<std::string> expected{"rolling16",        "offbeat",   "gallop", "rolling16_harmonic",
                                            "offbeat_changes",  "long_tied", "rumble", "hard_offbeat",
                                            "roll16_aggressive"};
    std::vector<std::string> ids;
    for (const Archetype& archetype : allArchetypes()) {
        ids.emplace_back(archetype.id);
        CHECK(archetype.role == VoiceRole::Bass);
        CHECK(archetype.ignoresKick == (archetype.id == "long_tied"));
        CHECK(archetype.ignoresKick == ignoresKickArchetype(archetype.id));
        CHECK(findArchetype(archetype.id) == &archetype);
    }
    CHECK(ids == expected);
    CHECK(findArchetype("stabs") == nullptr);
    CHECK(findArchetype("") == nullptr);
    CHECK(archetypeContour("unknown") == VelocityContour{});
}

TEST_CASE("rolling16 plays the free 16ths with short notes on 4otf", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Pattern p = makePattern(2);
        const auto notes = bassOf(p, "rolling16", style, seed, 50);
        // energy 50: ceil(12 * 700 / 1000) = 9 of the 12 free steps per bar, the offbeats 2, 6, 10, 14 always
        for (uint32_t bar = 0; bar < 2; ++bar) {
            const Steps steps = startsInBar(notes, bar);
            CHECK(steps.size() == 9);
            for (const uint32_t step : {2u, 6u, 10u, 14u}) {
                CHECK(std::count(steps.begin(), steps.end(), step) == 1);
            }
            for (const uint32_t step : steps) {
                CHECK(step % 4 != 0);
            }
        }
        for (const Note& note : notes) {
            const uint32_t step = (note.startTick % kTicksPerBar) / kStep;
            // 50 % or 75 % of a step; the last 16th before a kick is cut to the 1/32 clearance (120)
            if (step % 4 == 3) {
                CHECK(note.lengthTicks == 120);
            } else {
                CHECK((note.lengthTicks == 120 || note.lengthTicks == 180));
            }
        }
    }
}

TEST_CASE("rolling16 density follows the energy", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    const std::map<int, size_t> expected{{0, 5}, {30, 7}, {60, 10}, {90, 12}, {100, 12}};
    for (const auto& [energy, count] : expected) {
        Pattern p = makePattern(1);
        CHECK(bassOf(p, "rolling16", style, 3, energy).size() == count);
    }
    // four-on-the-floor with a pickup kick on step 15: 11 free steps, 100 % of them at energy 100
    Pattern pickup = makePattern(1, "4otf_pickup");
    const auto notes = bassOf(pickup, "rolling16", style, 3, 100);
    CHECK(notes.size() == 11);
    CHECK(std::none_of(notes.begin(), notes.end(), [](const Note& n) { return n.startTick == 15 * kStep; }));
}

TEST_CASE("rolling16 accents only offbeats, more often with energy", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    size_t accentsLow = 0;
    size_t accentsHigh = 0;
    for (uint64_t seed = 1; seed <= 400; ++seed) {
        for (const int energy : {0, 100}) {
            Pattern p = makePattern(1);
            for (const Note& note : bassOf(p, "rolling16", style, seed, energy)) {
                const uint32_t step = note.startTick / kStep;
                if (note.accent) {
                    CHECK(step % 4 == 2);
                    ++(energy == 0 ? accentsLow : accentsHigh);
                }
            }
        }
    }
    // 4 offbeat notes per bar and seed: 30 % at energy 0, 90 % at energy 100
    CHECK(accentsLow > 400 * 4 * 22 / 100);
    CHECK(accentsLow < 400 * 4 * 38 / 100);
    CHECK(accentsHigh > 400 * 4 * 82 / 100);
    CHECK(accentsHigh < 400 * 4 * 98 / 100);
}

TEST_CASE("offbeat sits on 2, 6, 10, 14 with even velocity", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Pattern p = makePattern(2);
        const auto notes = bassOf(p, "offbeat", style, seed, 50);
        CHECK(notes.size() == 8);
        for (uint32_t bar = 0; bar < 2; ++bar) {
            CHECK(startsInBar(notes, bar) == Steps{2, 6, 10, 14});
        }
        for (const Note& note : notes) {
            CHECK((note.lengthTicks == 240 || note.lengthTicks == 360));
            CHECK(note.velocity == 100);
            CHECK_FALSE(note.accent);
        }
    }
}

TEST_CASE("the energy moves the velocity level of an archetype", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    const std::map<int, int> expected{{0, 88}, {50, 100}, {100, 112}};
    for (const auto& [energy, velocity] : expected) {
        Pattern p = makePattern(1);
        for (const Note& note : bassOf(p, "offbeat", style, 5, energy)) {
            CHECK(note.velocity == velocity);
        }
    }
}

TEST_CASE("gallop plays pairs with a quieter second note", "[archetype]") {
    StyleProfile style = loadShipped("peak_time");
    Pattern p = makePattern(1);
    const auto notes = bassOf(p, "gallop", style, 7, 50);
    REQUIRE(notes.size() == 8);
    const Steps expectedSteps{2, 3, 6, 7, 10, 11, 14, 15};
    CHECK(startsInBar(notes, 0) == expectedSteps);
    for (size_t i = 0; i < notes.size(); ++i) {
        if (i % 2 == 0) {
            CHECK(notes[i].lengthTicks == 240);
            CHECK(notes[i].velocity == 100);
        } else {
            CHECK(notes[i].lengthTicks == 120); // 180 cut to the clearance before the next kick
            CHECK(notes[i].velocity == 80);
        }
    }
    style.bass.kickClearanceTicks = 0;
    Pattern free = makePattern(1);
    const auto open = bassOf(free, "gallop", style, 7, 50);
    for (size_t i = 1; i < open.size(); i += 2) {
        CHECK(open[i].lengthTicks == 180);
    }
}

TEST_CASE("rumble starts one step after every kick and lasts until the clearance", "[archetype]") {
    const StyleProfile style = loadShipped("hard_industrial");
    {
        Pattern p = makePattern(2);
        const auto notes = bassOf(p, "rumble", style, 11, 50);
        REQUIRE(notes.size() == 8);
        for (size_t i = 0; i < notes.size(); ++i) {
            CHECK(notes[i].startTick == i * 960 + 240);
            CHECK(notes[i].lengthTicks == 600); // 960 - 240 - 120
            CHECK(notes[i].pitch == 33);        // root A, lowest in 28-50
            CHECK(notes[i].velocity == 100);
        }
    }
    {
        Pattern p = makePattern(1, "halftime");
        const auto notes = bassOf(p, "rumble", style, 11, 50);
        REQUIRE(notes.size() == 2);
        CHECK(notes[0].startTick == 240);
        CHECK(notes[0].lengthTicks == 1560); // kick at 1920 minus 240 minus 120
        CHECK(notes[1].startTick == 2160);
        CHECK(notes[1].lengthTicks == 1560); // up to the kick of the next loop pass at 3840
    }
    {
        // custom kicks on 0, 1 and 8: the step after the first kick is a kick itself and stays empty
        Pattern p = makePattern(1, "custom");
        RhythmReference ref;
        ref.bars = 1;
        for (const size_t step : {0u, 1u, 8u}) {
            ref.kickSteps.set(step);
        }
        p.rhythmRef = ref;
        const auto notes = bassOf(p, "rumble", style, 11, 50);
        REQUIRE(notes.size() == 2);
        CHECK(notes[0].startTick == 2 * 240);
        CHECK(notes[0].lengthTicks == 1800 - 480); // up to the kick at 1920 minus the clearance
        CHECK(notes[1].startTick == 9 * 240);
        CHECK(notes[1].lengthTicks == 3720 - 2160); // up to the kick of the next loop pass at 3840 minus the clearance
    }
    {
        // broken_a has kicks on 0, 6, 8 and 12; the step after the kick on 6 gets a short note before the kick on 8
        Pattern p = makePattern(1, "broken_a");
        const auto notes = bassOf(p, "rumble", style, 11, 50);
        CHECK(startsInBar(notes, 0) == Steps{1, 7, 9, 13});
    }
}

TEST_CASE("rumble plays only the root, whatever the movement says", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time"); // 25 % fifth/octave, 5 % passing
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern p = makePattern(2);
        for (const Note& note : bassOf(p, "rumble", style, seed)) {
            CHECK(note.pitch == 33);
        }
    }
}

TEST_CASE("hard_offbeat and roll16_aggressive are louder", "[archetype]") {
    const StyleProfile style = loadShipped("hard_industrial");
    Pattern a = makePattern(1);
    const auto hard = bassOf(a, "hard_offbeat", style, 3, 50);
    CHECK(startsInBar(hard, 0) == Steps{2, 6, 10, 14});
    for (const Note& note : hard) {
        CHECK(note.velocity == 115);
        CHECK((note.lengthTicks == 120 || note.lengthTicks == 180));
    }
    Pattern b = makePattern(1);
    const auto aggressive = bassOf(b, "roll16_aggressive", style, 3, 50);
    CHECK(aggressive.size() == 9);
    for (const Note& note : aggressive) {
        CHECK(note.velocity == 110);
    }
}

TEST_CASE("roll16_aggressive accents more and jumps octaves now and then", "[archetype]") {
    const StyleProfile style = loadShipped("hard_industrial");
    size_t accents = 0;
    size_t notesTotal = 0;
    size_t octaves = 0;
    for (uint64_t seed = 1; seed <= 500; ++seed) {
        Pattern p = makePattern(1);
        for (const Note& note : bassOf(p, "roll16_aggressive", style, seed, 0)) {
            ++notesTotal;
            accents += note.accent ? 1 : 0;
            octaves += note.pitch == 45 ? 1 : 0;
        }
    }
    // energy 0: 50 % of the offbeats (4 of the 5 notes) accented; octave jumps 5 % plus 5 % of the octave choices
    CHECK(accents > 500 * 4 * 42 / 100);
    CHECK(accents < 500 * 4 * 58 / 100);
    CHECK(octaves > notesTotal * 5 / 100);
    CHECK(octaves < notesTotal * 14 / 100);
}

TEST_CASE("long_tied holds whole notes on the chord roots over the kicks", "[archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    {
        Pattern p = makePattern(2);
        const auto notes = bassOf(p, "long_tied", style, 1, 50, 0);
        REQUIRE(notes.size() == 2);
        CHECK(notes[0].startTick == 0); // on a kick
        CHECK(notes[0].lengthTicks == kTicksPerBar);
        CHECK(notes[1].startTick == kTicksPerBar);
        CHECK(notes[1].lengthTicks == kTicksPerBar);
        CHECK(notes[0].pitch == 33);
        CHECK(p.voices[0].archetypeId == "long_tied");
    }
    {
        // chord change in the middle of the second bar: two half notes, G (31) after A (33)
        Pattern p = makePattern(2);
        setProgression(p, {{0, ChordQuality::Minor, 3}, {10, ChordQuality::Major, 1}});
        const auto notes = bassOf(p, "long_tied", style, 1, 50, 0);
        REQUIRE(notes.size() == 3);
        CHECK(notes[1].startTick == kTicksPerBar);
        CHECK(notes[1].lengthTicks == kHalfBar);
        CHECK(notes[1].pitch == 33);
        CHECK(notes[2].startTick == kTicksPerBar + kHalfBar);
        CHECK(notes[2].lengthTicks == kHalfBar);
        CHECK(notes[2].pitch == 31);
    }
}

TEST_CASE("long_tied slides only between different pitches and never off the last note", "[archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    size_t slides = 0;
    size_t candidates = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(4);
        setProgression(p, {{0, ChordQuality::Minor, 2},
                           {8, ChordQuality::Major, 2},
                           {5, ChordQuality::Minor, 2},
                           {0, ChordQuality::Minor, 2}});
        const auto notes = bassOf(p, "long_tied", style, seed, 50, 0);
        REQUIRE(notes.size() == 4);
        CHECK_FALSE(notes.back().slide);
        for (size_t i = 0; i + 1 < notes.size(); ++i) {
            ++candidates;
            slides += notes[i].slide ? 1 : 0;
            if (notes[i].pitch == notes[i + 1].pitch) {
                CHECK_FALSE(notes[i].slide);
            }
        }
    }
    // 3 candidate slides per pattern, each at 50 %
    CHECK(slides > candidates * 45 / 100);
    CHECK(slides < candidates * 55 / 100);
}

TEST_CASE("long_tied splits a variation bar in two half notes", "[archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    Pattern p = makePattern(4);
    const auto notes = bassOf(p, "long_tied", style, 9, 50, 100);
    REQUIRE(notes.size() == 5);
    CHECK(notes[3].startTick == 3 * kTicksPerBar);
    CHECK(notes[3].lengthTicks == kHalfBar);
    CHECK(notes[3].pitch == 33);
    CHECK(notes[4].startTick == 3 * kTicksPerBar + kHalfBar);
    CHECK((notes[4].pitch == 40 || notes[4].pitch == 45));
    Pattern none = makePattern(4);
    CHECK(bassOf(none, "long_tied", style, 9, 50, 0).size() == 4);
    std::set<int> seconds;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern q = makePattern(4);
        const auto varied = bassOf(q, "long_tied", style, seed, 50, 100);
        REQUIRE(varied.size() == 5);
        seconds.insert(varied[4].pitch);
    }
    CHECK(seconds == std::set<int>{40, 45});
}

TEST_CASE("the pitches follow the movement of the profile", "[archetype]") {
    struct Case {
        const char* style;
        int root;
        int fifthOctave;
        int passing;
    };
    for (const Case c : {Case{"peak_time", 70, 25, 5}, Case{"hard_industrial", 90, 5, 5}}) {
        const StyleProfile style = loadShipped(c.style);
        std::map<int, size_t> counts;
        size_t total = 0;
        for (uint64_t seed = 1; seed <= 1500; ++seed) {
            Pattern p = makePattern(1);
            p.context.scaleId = std::string(style.scales.front().id);
            for (const Note& note : bassOf(p, "offbeat", style, seed, 50)) {
                ++counts[note.pitch];
                ++total;
            }
        }
        // offbeat plays on weak steps only, so the draws follow the weights exactly
        const size_t root = counts[33];
        const size_t fifthOctave = counts[40] + counts[45];
        const size_t passing = counts[36] + counts[43] + counts[34];
        INFO(c.style);
        CHECK(root + fifthOctave + passing == total);
        CHECK(root * 100 > total * static_cast<size_t>(c.root - 3));
        CHECK(root * 100 < total * static_cast<size_t>(c.root + 3));
        CHECK(fifthOctave * 100 > total * static_cast<size_t>(c.fifthOctave - 3));
        CHECK(fifthOctave * 100 < total * static_cast<size_t>(c.fifthOctave + 3));
        CHECK(passing * 100 < total * static_cast<size_t>(c.passing + 3));
        // fifth and octave are chosen about equally often
        CHECK(counts[40] * 100 > fifthOctave * 40);
        CHECK(counts[40] * 100 < fifthOctave * 60);
        CHECK(counts[45] * 100 > fifthOctave * 40);
        CHECK(counts[45] * 100 < fifthOctave * 60);
    }
}

TEST_CASE("strong steps never take a passing tone", "[archetype]") {
    StyleProfile style = loadShipped("peak_time");
    style.bass.movement = {0, 0, 100}; // only passing tones where allowed
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        Pattern p = makePattern(1, "halftime"); // kicks on 0 and 8 only, so steps 4 and 12 are free and strong
        for (const Note& note : bassOf(p, "rolling16", style, seed, 100)) {
            const uint32_t step = note.startTick / kStep;
            if (step % 4 == 0) {
                CHECK(note.pitch == 33); // the root, the fallback when no passing weight is left
            } else {
                CHECK((note.pitch == 36 || note.pitch == 43));
            }
        }
    }
}

TEST_CASE("the first note of every chord is its root in Melodic Techno", "[archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    REQUIRE(style.bass.movement.rootOnChordChange);
    for (const char* id : {"rolling16_harmonic", "offbeat_changes"}) {
        for (uint64_t seed = 1; seed <= 100; ++seed) {
            Pattern p = makePattern(4);
            setProgression(p, {{0, ChordQuality::Minor, 4}, {10, ChordQuality::Major, 2}, {8, ChordQuality::Major, 2}});
            const auto notes = bassOf(p, id, style, seed, 70);
            const std::vector<std::pair<uint32_t, int>> changes{
                {0, 33}, {2 * kTicksPerBar, 31}, {3 * kTicksPerBar, 29}};
            for (const auto& [tick, pitch] : changes) {
                const auto first =
                    std::find_if(notes.begin(), notes.end(), [&](const Note& n) { return n.startTick >= tick; });
                REQUIRE(first != notes.end());
                INFO(id << " seed " << seed << " tick " << tick);
                CHECK(first->pitch == pitch);
            }
        }
    }
}

TEST_CASE("rolling16_harmonic leads into a chord change with a scale tone next to the new root", "[archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    size_t approaches = 0;
    const int target = 31; // G, root of the second chord
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = makePattern(2);
        setProgression(p, {{0, ChordQuality::Minor, 2}, {10, ChordQuality::Major, 2}});
        const auto notes = bassOf(p, "rolling16_harmonic", style, seed, 100);
        const auto last =
            std::find_if(notes.rbegin(), notes.rend(), [](const Note& n) { return n.startTick < kTicksPerBar; });
        REQUIRE(last != notes.rend());
        CHECK(last->startTick + 960 >= kTicksPerBar);
        if (last->pitch != 33 && last->pitch != 40 && last->pitch != 45 && last->pitch != 36 && last->pitch != 43) {
            CHECK((last->pitch == target - 1 || last->pitch == target - 2 || last->pitch == target + 1));
            ++approaches;
        }
    }
    CHECK(approaches > 200 * 40 / 100);
    CHECK(approaches < 200 * 80 / 100);
    // offbeat_changes and rolling16 do not use approach tones
    for (const char* id : {"offbeat_changes", "rolling16"}) {
        for (uint64_t seed = 1; seed <= 50; ++seed) {
            Pattern p = makePattern(2);
            setProgression(p, {{0, ChordQuality::Minor, 2}, {10, ChordQuality::Major, 2}});
            StyleProfile plain = style;
            plain.bass.movement = {100, 0, 0};
            plain.bass.movement.rootOnChordChange = false;
            for (const Note& note : bassOf(p, id, plain, seed, 100)) {
                CHECK(note.pitch == (note.startTick < kTicksPerBar ? 33 : 31));
            }
        }
    }
}

TEST_CASE("notes follow the chord that sounds, not only the first one", "[archetype]") {
    StyleProfile style = loadShipped("peak_time");
    style.bass.movement = {100, 0, 0};
    Pattern p = makePattern(2);
    setProgression(p, {{0, ChordQuality::Minor, 2}, {5, ChordQuality::Minor, 1}, {7, ChordQuality::Major, 1}});
    for (const Note& note : bassOf(p, "offbeat", style, 1)) {
        const int expected = note.startTick < kTicksPerBar ? 33 : note.startTick < kTicksPerBar + kHalfBar ? 38 : 28;
        CHECK(note.pitch == expected);
    }
}

TEST_CASE("the cell repeats in every bar, only the last bar of four varies", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    int varied = 0;
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        Pattern p = makePattern(8);
        const auto notes = bassOf(p, "rolling16", style, seed, 50, 100);
        std::vector<Steps> bars;
        for (uint32_t bar = 0; bar < 8; ++bar) {
            bars.push_back(startsInBar(notes, bar));
        }
        for (const uint32_t bar : {1u, 2u, 5u, 6u}) {
            CHECK(bars[bar] == bars[0]);
        }
        CHECK(bars[4] == bars[0]);
        // creativity 100: the bars 3 and 7 always vary, one note moves to another free step
        for (const uint32_t bar : {3u, 7u}) {
            CHECK(bars[bar] != bars[0]);
            CHECK(bars[bar].size() == bars[0].size());
        }
        varied += bars[3] != bars[0] ? 1 : 0;
        // creativity 0 and short patterns never vary
        Pattern calm = makePattern(8);
        const auto calmNotes = bassOf(calm, "rolling16", style, seed, 50, 0);
        for (uint32_t bar = 1; bar < 8; ++bar) {
            CHECK(startsInBar(calmNotes, bar) == startsInBar(calmNotes, 0));
        }
        Pattern shortPattern = makePattern(2);
        const auto shortNotes = bassOf(shortPattern, "rolling16", style, seed, 50, 100);
        CHECK(startsInBar(shortNotes, 1) == startsInBar(shortNotes, 0));
    }
    CHECK(varied == 100);
}

TEST_CASE("octave offsets move the line by whole octaves", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern base = makePattern(2);
    const auto reference = bassOf(base, "rolling16", style, 4);
    Pattern shifted = makePattern(2);
    shifted.voices[0].octaveOffset = 1;
    const auto notes = bassOf(shifted, "rolling16", style, 4);
    REQUIRE(notes.size() == reference.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        CHECK(notes[i].startTick == reference[i].startTick);
        CHECK(notes[i].pitch == reference[i].pitch + 12);
    }
}

TEST_CASE("generateVoice assigns ids, replaces the old notes and refuses invalid requests", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makePattern(1);
    Note old;
    old.id = allocateNoteId(p);
    old.pitch = 40;
    old.startTick = 100;
    old.lengthTicks = 100;
    p.voices[0].notes.push_back(old);
    const uint32_t nextBefore = p.nextNoteId;
    Pcg32 rng = Pcg32::fromSeed(1);
    REQUIRE(generateVoice(p, 0, "offbeat", style, {}, rng));
    const auto& notes = p.voices[0].notes;
    REQUIRE(notes.size() == 4);
    std::set<uint32_t> ids;
    for (const Note& note : notes) {
        CHECK(note.id >= nextBefore);
        ids.insert(note.id);
    }
    CHECK(ids.size() == 4);
    CHECK(p.nextNoteId == nextBefore + 4);
    CHECK(p.voices[0].archetypeId == "offbeat");

    const Pattern before = p;
    CHECK_FALSE(generateVoice(p, 0, "nonsense", style, {}, rng));
    CHECK_FALSE(generateVoice(p, 5, "offbeat", style, {}, rng));
    CHECK_FALSE(generateVoice(p, 1, "offbeat", style, {}, rng)); // voice 1 is the melody
    p.context.scaleId = "no_such_scale";
    CHECK_FALSE(generateVoice(p, 0, "offbeat", style, {}, rng));
    p.context.scaleId = before.context.scaleId;
    CHECK(p == before);
}

TEST_CASE("the same seed gives the same line, other seeds differ", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    std::set<std::vector<uint8_t>> shapes;
    for (const char* id : {"rolling16", "gallop", "long_tied", "rumble"}) {
        Pattern a = makePattern(4);
        Pattern b = makePattern(4);
        bassOf(a, id, style, 77);
        bassOf(b, id, style, 77);
        CHECK(a == b);
    }
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern p = makePattern(2);
        std::vector<uint8_t> shape;
        for (const Note& note : bassOf(p, "rolling16", style, seed, 50)) {
            shape.push_back(static_cast<uint8_t>(note.startTick / kStep));
            shape.push_back(note.pitch);
        }
        shapes.insert(shape);
    }
    CHECK(shapes.size() > 15);
}

TEST_CASE("every archetype keeps off the kicks and ends before the next one on every grid", "[archetype]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const std::string grid :
             {"4otf", "4otf_pickup", "halftime", "broken_a", "broken_b", "custom", "unknown"}) {
            for (const Archetype& archetype : allArchetypes()) {
                if (archetype.ignoresKick) {
                    continue;
                }
                for (uint64_t seed = 1; seed <= 8; ++seed) {
                    Pattern p = makePattern(2, grid);
                    if (grid == "custom") {
                        RhythmReference ref;
                        ref.bars = 1;
                        for (const size_t step : {0u, 3u, 6u, 9u, 12u}) {
                            ref.kickSteps.set(step);
                        }
                        p.rhythmRef = ref;
                    }
                    const auto kicks = kickTicks(p);
                    const auto notes = bassOf(p, std::string(archetype.id), style, seed, static_cast<int>(seed * 12));
                    for (const Note& note : notes) {
                        INFO(styleName << " " << archetype.id << " " << grid << " seed " << seed << " tick "
                                       << note.startTick);
                        CHECK_FALSE(isKickTick(kicks, note.startTick));
                        const auto next = std::upper_bound(kicks.begin(), kicks.end(), note.startTick);
                        const uint32_t nextKick =
                            next != kicks.end() ? *next : kicks.front() + p.lengthBars * kTicksPerBar;
                        CHECK(note.startTick + note.lengthTicks + style.bass.kickClearanceTicks <= nextKick);
                        CHECK(note.startTick + note.lengthTicks <= p.lengthBars * kTicksPerBar);
                        CHECK(note.lengthTicks >= 60);
                    }
                }
            }
        }
    }
}

TEST_CASE("a phrase with its own kick grid changes the free steps of its bars", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makePattern(8);
    p.phrases.clear();
    p.phrases.push_back(makePhrase(0, 4, PhraseRole::Main));
    p.phrases.push_back(makePhrase(4, 4, PhraseRole::Variation));
    p.phrases.back().kickGridId = "halftime";
    const auto notes = bassOf(p, "rolling16", style, 3, 100);
    CHECK(startsInBar(notes, 0).size() == 12);
    CHECK(startsInBar(notes, 4).size() == 14); // steps 0 and 8 are the only kicks
    const auto steps = startsInBar(notes, 4);
    CHECK(std::count(steps.begin(), steps.end(), 4u) == 1);
}

TEST_CASE("generated lines are valid and need no repair from the constraint layer", "[archetype]") {
    const std::vector<std::vector<ChordSpan>> harmony{
        {{0, ChordQuality::Minor, 1}},
        {{0, ChordQuality::Minor, 3}, {10, ChordQuality::Major, 1}},
    };
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const Archetype& archetype : allArchetypes()) {
            for (const uint32_t bars : {1u, 2u, 4u, 8u}) {
                for (const int energy : {0, 30, 60, 90}) {
                    for (uint64_t seed = 1; seed <= 6; ++seed) {
                        Pattern p = makePattern(bars);
                        p.context.scaleId = std::string(style.scales.front().id);
                        std::vector<ChordSpan> spans;
                        static const std::vector<ChordSpan> cycle{{0, ChordQuality::Minor, 2},
                                                                  {10, ChordQuality::Major, 1},
                                                                  {5, ChordQuality::Diminished, 1},
                                                                  {8, ChordQuality::Sus2, 1},
                                                                  {7, ChordQuality::Sus4, 1}};
                        uint32_t covered = 0;
                        for (size_t i = 0; covered < bars * 2; ++i) {
                            ChordSpan span = cycle[(i + seed) % cycle.size()];
                            span.halfBars = std::min(span.halfBars, bars * 2 - covered);
                            covered += span.halfBars;
                            spans.push_back(span);
                        }
                        setProgression(p, spans);
                        const auto notes = bassOf(p, std::string(archetype.id), style, seed, energy, 50);
                        INFO(styleName << " " << archetype.id << " bars " << bars << " energy " << energy << " seed "
                                       << seed);
                        const auto range =
                            *effectiveRange(style.registerProfile(), VoiceRole::Bass, archetype.id, p.voicing, 0);
                        const Scale& scale = *findScale(p.context.scaleId);
                        for (size_t i = 0; i < notes.size(); ++i) {
                            const Note& note = notes[i];
                            CHECK(range.contains(note.pitch));
                            const uint32_t half = note.startTick / kHalfBar;
                            std::optional<Chord> chord;
                            for (const ChordEvent& event : p.context.progression) {
                                if (half >= event.startHalfBar && half < event.startHalfBar + event.lengthHalfBars) {
                                    chord = event.chord;
                                }
                            }
                            CHECK(isAllowed(scale, p.context.root, chord, note.pitch));
                            if (i + 1 < notes.size()) {
                                CHECK(note.startTick + note.lengthTicks <= notes[i + 1].startTick);
                            }
                            CHECK(note.startTick + note.lengthTicks <= bars * kTicksPerBar);
                            CHECK(note.velocity >= 1);
                            CHECK(note.velocity <= 127);
                        }
                        Pattern repaired = p;
                        const ConstraintReport report =
                            applyConstraints(repaired, constraintSettingsFor(repaired, style));
                        CHECK(report.total() == 0);
                        CHECK(repaired == p);
                    }
                }
            }
        }
    }
}

TEST_CASE("the 16th archetypes stay inside the density band of the energy", "[archetype]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const char* id : {"rolling16", "rolling16_harmonic", "roll16_aggressive"}) {
            for (const int energy : {0, 30, 60, 90, 100}) {
                for (uint64_t seed = 1; seed <= 10; ++seed) {
                    Pattern p = makePattern(4);
                    const auto notes = bassOf(p, id, style, seed, energy, 100);
                    const DensityBand band = densityBand(VoiceRole::Bass, energy);
                    const int perBar = static_cast<int>(notes.size()) * 100 / 4;
                    if (energy == 100) {
                        continue; // full density: the variation drops a note, one below the band edge is intended
                    }
                    INFO(styleName << " " << id << " energy " << energy << " seed " << seed << " notes "
                                   << notes.size());
                    CHECK(perBar >= band.low);
                    CHECK(perBar <= band.high);
                }
            }
        }
    }
}

TEST_CASE("fifth, octave and passing tones respect the range and the scale", "[archetype]") {
    StyleProfile style = loadShipped("peak_time");
    {
        // range 28-39: the fifth above the root (40) folds down to 28, the octave does not fit and stays on the root
        style.bass.range = {28, 39};
        style.bass.movement = {0, 100, 0};
        std::set<int> pitches;
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Pattern p = makePattern(1);
            for (const Note& note : bassOf(p, "offbeat", style, seed)) {
                pitches.insert(note.pitch);
            }
        }
        CHECK(pitches == std::set<int>{28, 33});
    }
    {
        // range 28-45: the octave of the root 33 is exactly the top note
        style.bass.range = {28, 45};
        std::set<int> pitches;
        for (uint64_t seed = 1; seed <= 20; ++seed) {
            Pattern p = makePattern(1);
            for (const Note& note : bassOf(p, "offbeat", style, seed)) {
                pitches.insert(note.pitch);
            }
        }
        CHECK(pitches == std::set<int>{40, 45});
    }
    {
        // passing tones in A natural minor: the third (C = 36) and the minor seventh (G = 43), no tension tone
        style = loadShipped("peak_time");
        style.chromaticDefaultPercent = 29;
        style.bass.movement = {0, 0, 100};
        std::set<int> pitches;
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            Pattern p = makePattern(1);
            p.context.scaleId = "phrygian"; // A phrygian has a minor second (B flat = 34)
            for (const Note& note : bassOf(p, "offbeat", style, seed)) {
                pitches.insert(note.pitch);
            }
        }
        CHECK(pitches == std::set<int>{36, 43});
        // from 30 % chromatic share on the minor second joins in
        style.chromaticDefaultPercent = 30;
        pitches.clear();
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            Pattern p = makePattern(1);
            p.context.scaleId = "phrygian";
            for (const Note& note : bassOf(p, "offbeat", style, seed)) {
                pitches.insert(note.pitch);
            }
        }
        CHECK(pitches == std::set<int>{34, 36, 43});
    }
}

TEST_CASE("an octave jump does not replace a passing tone, and its share follows the energy", "[archetype]") {
    StyleProfile style = loadShipped("hard_industrial");
    style.bass.movement = {0, 0, 100};
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        Pattern p = makePattern(1);
        for (const Note& note : bassOf(p, "roll16_aggressive", style, seed, 100)) {
            CHECK(note.pitch != 45);
        }
    }
    style.bass.movement = {100, 0, 0};
    const std::map<int, std::pair<size_t, size_t>> window{{0, {44, 58}},
                                                          {100, {194, 206}}}; // permille bounds: 5 %, 20 %
    for (const auto& [energy, bounds] : window) {
        size_t octaves = 0;
        size_t total = 0;
        for (uint64_t seed = 1; seed <= 4000; ++seed) {
            Pattern p = makePattern(1);
            for (const Note& note : bassOf(p, "roll16_aggressive", style, seed, energy)) {
                ++total;
                octaves += note.pitch == 45 ? 1 : 0;
            }
        }
        INFO("energy " << energy);
        CHECK(octaves * 1000 > total * bounds.first);
        CHECK(octaves * 1000 < total * bounds.second);
    }
}

TEST_CASE("the minimum note length is 60 ticks", "[archetype]") {
    StyleProfile style = loadShipped("peak_time");
    style.bass.kickClearanceTicks = 180; // the 16th before a kick keeps 60 ticks
    Pattern p = makePattern(1);
    size_t beforeKick = 0;
    for (const Note& note : bassOf(p, "rolling16", style, 2, 100)) {
        if ((note.startTick / kStep) % 4 == 3) {
            CHECK(note.lengthTicks == 60);
            ++beforeKick;
        }
    }
    CHECK(beforeKick == 4);
    style.bass.kickClearanceTicks = 240; // nothing is left of them
    Pattern q = makePattern(1);
    for (const Note& note : bassOf(q, "rolling16", style, 2, 100)) {
        CHECK((note.startTick / kStep) % 4 != 3);
    }
}

TEST_CASE("rumble stops at the pattern end when the next kick lies in the next pass", "[archetype]") {
    const StyleProfile style = loadShipped("hard_industrial");
    Pattern p = makePattern(1, "custom");
    RhythmReference ref;
    ref.bars = 1;
    ref.kickSteps.set(3);
    p.rhythmRef = ref;
    const auto notes = bassOf(p, "rumble", style, 1);
    REQUIRE(notes.size() == 1);
    CHECK(notes[0].startTick == 960);
    CHECK(notes[0].lengthTicks == kTicksPerBar - 960);
}

TEST_CASE("a chord without any note passes its root on to nobody", "[archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    Pattern p = makePattern(1, "custom");
    RhythmReference ref;
    ref.bars = 1;
    for (size_t step = 0; step < 8; ++step) {
        ref.kickSteps.set(step); // the whole first half bar is kicks, so the first chord gets no note
    }
    p.rhythmRef = ref;
    setProgression(p, {{0, ChordQuality::Minor, 1}, {10, ChordQuality::Major, 1}});
    const auto notes = bassOf(p, "rolling16_harmonic", style, 1, 100);
    REQUIRE_FALSE(notes.empty());
    CHECK(notes.front().startTick >= kHalfBar);
    CHECK(notes.front().pitch == 31); // G, the root of the second chord
}

TEST_CASE("chord roots are forced only on real chord changes", "[archetype]") {
    StyleProfile style = loadShipped("melodic_techno");
    style.bass.movement = {0, 100, 0};
    style.bass.movement.rootOnChordChange = true;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern p = makePattern(2);
        const auto notes = bassOf(p, "rolling16_harmonic", style, seed, 100);
        size_t roots = 0;
        for (const Note& note : notes) {
            roots += note.pitch == 33 ? 1 : 0;
        }
        CHECK(roots == 1); // only the first note of the single chord
    }
}

TEST_CASE("the approach tone covers a note a beat before the change and all three candidates", "[archetype]") {
    StyleProfile style = loadShipped("melodic_techno");
    style.bass.movement = {100, 0, 0};
    style.bass.movement.rootOnChordChange = false;
    std::set<int> last;
    size_t approaches = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        // kicks on 13-15 leave step 12 (exactly a beat before the change) as the last free step of the bar
        Pattern p = makePattern(2, "custom");
        RhythmReference ref;
        ref.bars = 1;
        for (const size_t step : {13u, 14u, 15u}) {
            ref.kickSteps.set(step);
        }
        p.rhythmRef = ref;
        setProgression(p,
                       {{0, ChordQuality::Minor, 2}, {2, ChordQuality::Diminished, 2}}); // A minor, then B diminished
        const auto notes = bassOf(p, "rolling16_harmonic", style, seed, 100);
        const auto before =
            std::find_if(notes.rbegin(), notes.rend(), [](const Note& n) { return n.startTick < kTicksPerBar; });
        REQUIRE(before != notes.rend());
        CHECK(before->startTick == 12 * kStep);
        if (before->pitch != 33) {
            ++approaches;
            last.insert(before->pitch);
        }
    }
    // the candidates 34 (not in A minor) and 33 (the old root, indistinguishable) leave 36 for a third of the 60 %
    CHECK(approaches > 200 * 10 / 100);
    CHECK(approaches < 200 * 30 / 100);
    CHECK(last == std::set<int>{36});
}

TEST_CASE("the quality rating accepts kick-aware lines", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    for (const Archetype& archetype : allArchetypes()) {
        Pattern p = makePattern(4);
        bassOf(p, std::string(archetype.id), style, 5, 60);
        const QualityContext context = qualityContextFor(p, style, {60, 40});
        CHECK(context.ignoresKick == std::vector<bool>{archetype.ignoresKick, false});
        CHECK(context.energyPct == 60);
        CHECK(context.chromaticPercent == style.chromaticDefaultPercent);
        CHECK(context.creativityPct == 40);
        CHECK(scoreCriteria(p, context).kick == 100);
    }
    // a long_tied line rated without the flag would be punished for starting on kicks
    Pattern tied = makePattern(4);
    bassOf(tied, "long_tied", style, 5, 60);
    CHECK(scoreCriteria(tied, QualityContext{}).kick < 100);
    CHECK(scoreCriteria(tied, qualityContextFor(tied, style, {})).kick == 100);
}

TEST_CASE("candidate selection works with the archetype generators", "[archetype]") {
    for (const std::string& styleName : kStyles) {
        const StyleProfile style = loadShipped(styleName);
        for (const Archetype& archetype : allArchetypes()) {
            const auto generate = [&](uint64_t seed) {
                Pattern p = makePattern(4);
                Pcg32 rng = Pcg32::fromSeed(seed);
                REQUIRE(generateVoice(p, 0, archetype.id, style, {60, 40}, rng));
                return p;
            };
            const SelectionResult result =
                selectBest(generate, 1234, style.quality, qualityContextFor(generate(1), style, {60, 40}));
            INFO(styleName << " " << archetype.id);
            CHECK(result.success);
            if (result.success) {
                CHECK(generate(result.seed) == result.pattern);
            }
        }
    }
}

TEST_CASE("the constraint settings carry the kick clearance and the chromatic share of the style", "[archetype]") {
    StyleProfile style = loadShipped("hard_industrial");
    style.bass.kickClearanceTicks = 60;
    Pattern p = makePattern(1);
    p.voices[0].archetypeId = "long_tied";
    const ConstraintSettings settings = constraintSettingsFor(p, style);
    CHECK(settings.kickClearanceTicks == 60);
    CHECK(settings.chromaticPercent == style.chromaticDefaultPercent);
    REQUIRE(settings.voices.size() == 2);
    CHECK(settings.voices[0].ignoresKick);
    CHECK_FALSE(settings.voices[1].ignoresKick);
    CHECK(settings.voices[0].rangeLow == style.bass.range.low);
    CHECK(settings.voices[0].rangeHigh == style.bass.range.high);
    p.voices[0].archetypeId = "rumble";
    CHECK_FALSE(constraintSettingsFor(p, style).voices[0].ignoresKick);
}

TEST_CASE("the constraint layer leaves a long_tied line on the kicks alone", "[archetype]") {
    Pattern p = makePattern(2);
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = 33;
    note.startTick = 0; // on a kick
    note.lengthTicks = kTicksPerBar;
    p.voices[0].notes.push_back(note);
    p.voices[0].archetypeId = "long_tied";
    Pattern other = p;
    other.voices[0].archetypeId = "offbeat";
    CHECK(applyConstraints(p, ConstraintSettings::defaultsFor(p)).total() == 0);
    CHECK(applyConstraints(other, ConstraintSettings::defaultsFor(other)).total() > 0);
}

TEST_CASE("the automatic choice follows the weights of the profile", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time"); // rolling16 4, offbeat 3, gallop 3
    std::map<std::string, size_t> counts;
    Pcg32 rng = Pcg32::fromSeed(99);
    for (int i = 0; i < 10000; ++i) {
        ++counts[chooseArchetype(style, VoiceRole::Bass, 50, rng)];
    }
    CHECK(counts.size() == 3);
    CHECK(counts["rolling16"] > 3700);
    CHECK(counts["rolling16"] < 4300);
    CHECK(counts["offbeat"] > 2700);
    CHECK(counts["offbeat"] < 3300);
    CHECK(counts["gallop"] > 2700);
    CHECK(counts["gallop"] < 3300);
}

TEST_CASE("energy pulls dense archetypes up and calm ones down", "[archetype]") {
    const StyleProfile peak = loadShipped("peak_time");
    const auto share = [](const StyleProfile& style, const std::string& id, int energy) {
        Pcg32 rng = Pcg32::fromSeed(5);
        size_t hits = 0;
        for (int i = 0; i < 10000; ++i) {
            hits += chooseArchetype(style, VoiceRole::Bass, energy, rng) == id ? 1 : 0;
        }
        return hits;
    };
    // rolling16 is dense: 4 * (50 + e) against 300 + 300
    CHECK(share(peak, "rolling16", 0) > 10000 * 18 / 100);
    CHECK(share(peak, "rolling16", 0) < 10000 * 32 / 100);
    CHECK(share(peak, "rolling16", 100) > 10000 * 45 / 100);
    CHECK(share(peak, "rolling16", 100) < 10000 * 55 / 100);
    // long_tied is calm: 2 * (150 - e) against 4 * (50 + e) and 3 * 100
    const StyleProfile melodic = loadShipped("melodic_techno");
    CHECK(share(melodic, "long_tied", 0) > 10000 * 33 / 100);   // 300 of 200 + 300 + 300
    CHECK(share(melodic, "long_tied", 100) < 10000 * 13 / 100); // 100 of 600 + 300 + 100
    // energy outside 0-100 counts as the nearest end
    CHECK(share(melodic, "long_tied", 500) == share(melodic, "long_tied", 100));
    CHECK(share(melodic, "long_tied", -5) == share(melodic, "long_tied", 0));
}

TEST_CASE("the automatic choice skips unknown ids, other roles and zero weights", "[archetype]") {
    StyleProfile style = loadShipped("peak_time");
    style.bass.archetypes = {{"bogus", 50}, {"stabs", 50}, {"offbeat", 1}, {"gallop", 0}, {"rumble", -3}};
    Pcg32 rng = Pcg32::fromSeed(1);
    for (int i = 0; i < 100; ++i) {
        CHECK(chooseArchetype(style, VoiceRole::Bass, 50, rng) == "offbeat");
    }
    style.bass.archetypes = {{"bogus", 5}};
    CHECK(chooseArchetype(style, VoiceRole::Bass, 50, rng).empty());
    style.bass.archetypes.clear();
    CHECK(chooseArchetype(style, VoiceRole::Bass, 50, rng).empty());
    // a bass archetype in the melody list does not count for the melody
    StyleProfile crossed = loadShipped("peak_time");
    crossed.melody.archetypes = {{"rumble", 5}};
    CHECK(chooseArchetype(crossed, VoiceRole::Melody, 50, rng).empty());
    // no melody archetype is implemented yet
    CHECK(chooseArchetype(loadShipped("peak_time"), VoiceRole::Melody, 50, rng).empty());
}

TEST_CASE("a manual archetype wins without a random draw", "[archetype]") {
    const StyleProfile style = loadShipped("peak_time");
    Track track;
    track.role = VoiceRole::Bass;
    track.archetypeAuto = false;
    track.archetypeId = "rumble"; // not in the profile, but implemented
    Pcg32 rng = Pcg32::fromSeed(8);
    Pcg32 untouched = Pcg32::fromSeed(8);
    CHECK(resolveArchetype(track, style, 50, rng) == "rumble");
    CHECK(rng.next() == untouched.next());

    // automatic, an unknown manual id and a manual id of another role fall back to the weighted choice
    Pcg32 a = Pcg32::fromSeed(8);
    const std::string expected = chooseArchetype(style, VoiceRole::Bass, 50, a);
    track.archetypeAuto = true;
    Pcg32 b = Pcg32::fromSeed(8);
    CHECK(resolveArchetype(track, style, 50, b) == expected);
    track.archetypeAuto = false;
    track.archetypeId = "nonsense";
    Pcg32 c = Pcg32::fromSeed(8);
    CHECK(resolveArchetype(track, style, 50, c) == expected);
    track.role = VoiceRole::Melody;
    track.archetypeId = "rumble";
    Pcg32 d = Pcg32::fromSeed(8);
    CHECK(resolveArchetype(track, style, 50, d).empty());
}
