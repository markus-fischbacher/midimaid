#include "PatternFixtures.h"
#include "SeriesCheck.h"
#include "core/Constraints.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
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

/// The stabs as chords: the pitches of each start, as intervals above the lowest tone.
std::map<uint32_t, std::vector<int>> shapesOf(const Track& track) {
    std::map<uint32_t, std::vector<int>> shapes;
    for (const Note& note : track.notes) {
        shapes[note.startTick].push_back(note.pitch);
    }
    for (auto& [start, pitches] : shapes) {
        std::sort(pitches.begin(), pitches.end());
        const int low = pitches.front();
        for (int& pitch : pitches) {
            pitch -= low;
        }
    }
    return shapes;
}

GenerationRequest stabsRequest(uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    request.melodyArchetype = "stabs";
    return request;
}

Note& add(Pattern& p, size_t voice, int pitch, uint32_t start, uint32_t length) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

} // namespace

TEST_CASE("the style decides whether a generated pattern uses chord memory", "[chord-memory][generator]") {
    for (const std::string name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        const Pattern p = generateCandidate(style, stabsRequest(3), 3);
        INFO(name);
        CHECK(p.voicing.chordMemory == style.melody.chordMemory);
        CHECK(p.voicing.chordMemory == (name != "melodic_techno"));
    }
}

TEST_CASE("stabs with chord memory repeat one shape over the whole progression", "[chord-memory][generator]") {
    for (const std::string name : {"peak_time", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        int checked = 0;
        int stabs = 0;
        int conforming = 0;
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            const Pattern p = generateCandidate(style, stabsRequest(seed), seed);
            const Track& melody = p.voices[1];
            if (melody.archetypeId != "stabs") {
                continue;
            }
            const auto shapes = shapesOf(melody);
            if (shapes.empty()) {
                continue;
            }
            ++checked;
            // every stab has the shape of the first one; the constraint layer may drop a tone of a hit, and a stab that
            // cannot move away from a clash with the bass has its tones changed one by one (rare)
            const auto& first = shapes.begin()->second;
            for (const auto& [start, shape] : shapes) {
                ++stabs;
                conforming += std::includes(first.begin(), first.end(), shape.begin(), shape.end()) ? 1 : 0;
            }
        }
        INFO(name);
        CHECK(checked > 20);
        CHECK(conforming * 100 >= stabs * 90);
    }
}

TEST_CASE("melodic techno keeps its voice leading", "[chord-memory][generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    int differentShapes = 0;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        const Pattern p = generateCandidate(style, stabsRequest(seed), seed);
        std::set<std::vector<int>> shapes;
        for (const auto& [start, shape] : shapesOf(p.voices[1])) {
            shapes.insert(shape);
        }
        differentShapes += shapes.size() > 1 ? 1 : 0;
    }
    CHECK(differentShapes > 10); // the shapes follow the chords
}

TEST_CASE("the constraint layer leaves the tones of a memory stab alone", "[chord-memory][constraints]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    p.context.scaleId = "natural_minor"; // A minor: no A# and no C#
    p.voices[1].archetypeId = "stabs";
    add(p, 1, 70, 240, 240); // A#
    add(p, 1, 73, 240, 240); // C#
    add(p, 1, 76, 240, 240); // E
    Pattern memory = p;
    memory.voicing.chordMemory = true;
    applyConstraints(memory, ConstraintSettings::defaultsFor(memory));
    std::set<int> kept;
    for (const Note& n : memory.voices[1].notes) {
        kept.insert(n.pitch);
    }
    CHECK(kept == std::set<int>{70, 73, 76});

    Pattern plain = p; // without the mode they snap to the scale
    applyConstraints(plain, ConstraintSettings::defaultsFor(plain));
    std::set<int> snapped;
    for (const Note& n : plain.voices[1].notes) {
        snapped.insert(n.pitch);
    }
    CHECK(snapped != kept);
    Pattern aggro = p; // the aggressive stabs are exempt as well
    aggro.voicing.chordMemory = true;
    aggro.voices[1].archetypeId = "aggro_stabs";
    applyConstraints(aggro, ConstraintSettings::defaultsFor(aggro));
    std::set<int> aggroPitches;
    for (const Note& n : aggro.voices[1].notes) {
        aggroPitches.insert(n.pitch);
    }
    CHECK(aggroPitches == kept);
    // another archetype in a memory pattern is not exempt
    Pattern other = p;
    other.voicing.chordMemory = true;
    other.voices[1].archetypeId = "hypnotic_motif";
    applyConstraints(other, ConstraintSettings::defaultsFor(other));
    std::set<int> otherPitches;
    for (const Note& n : other.voices[1].notes) {
        otherPitches.insert(n.pitch);
    }
    CHECK(otherPitches != kept);
}

TEST_CASE("a memory stab that clashes with the bass moves to the next 16th as a whole", "[chord-memory][constraints]") {
    // bass A (45) holds over the beat; the stab on the beat has a B flat above it (a tense minor second class)
    auto build = [](bool memory) {
        Pattern p = makeEmptyPattern(1, "peak_time");
        p.voicing.chordMemory = memory;
        p.voices[1].archetypeId = "stabs";
        add(p, 0, 33, 480, 960);        // bass A from the off-beat, sounding over the beat
        add(p, 1, 70, 960, 240);        // A#: a minor second class over the bass on the strong step
        add(p, 1, 74, 960, 240);
        add(p, 1, 77, 960, 240);
        return p;
    };
    Pattern memory = build(true);
    ConstraintSettings settings = ConstraintSettings::defaultsFor(memory);
    settings.kickClearanceTicks = 0; // the bass keeps sounding over the beat
    const ConstraintReport report = applyConstraints(memory, settings);
    CHECK(report.intervalsFixed == 1); // one stab moved
    std::set<uint32_t> starts;
    std::set<int> pitches;
    for (const Note& n : memory.voices[1].notes) {
        starts.insert(n.startTick);
        pitches.insert(n.pitch);
    }
    // the stab moved, its tones stayed
    CHECK(starts == std::set<uint32_t>{960 + 240});
    CHECK(pitches == std::set<int>{70, 74, 77});
    Pattern plain = build(false);
    settings = ConstraintSettings::defaultsFor(plain);
    settings.kickClearanceTicks = 0;
    applyConstraints(plain, settings);
    std::set<int> plainPitches;
    for (const Note& n : plain.voices[1].notes) {
        plainPitches.insert(n.pitch);
    }
    CHECK(plainPitches != pitches); // the tones were changed instead
}

TEST_CASE("a memory stab stays when the next 16th would clash as well", "[chord-memory][constraints]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    p.voicing.chordMemory = true;
    p.voices[1].archetypeId = "stabs";
    add(p, 0, 33, 480, 2400);   // the bass sounds over the beats at 960 and 1920
    add(p, 1, 70, 960, 960);    // a long stab: from 1200 it would still reach the beat at 1920
    add(p, 1, 74, 960, 960);
    ConstraintSettings settings = ConstraintSettings::defaultsFor(p);
    settings.kickClearanceTicks = 0;
    applyConstraints(p, settings);
    for (const Note& n : p.voices[1].notes) {
        CHECK(n.startTick == 960);
    }
}

TEST_CASE("a memory stab with locks, or a taken next step, does not move", "[chord-memory][constraints]") {
    auto build = [] {
        Pattern p = makeEmptyPattern(1, "peak_time");
        p.voicing.chordMemory = true;
        p.voices[1].archetypeId = "stabs";
        add(p, 0, 33, 480, 960);
        add(p, 1, 70, 960, 240);
        add(p, 1, 74, 960, 240);
        return p;
    };
    const auto run = [](Pattern& p) {
        ConstraintSettings settings = ConstraintSettings::defaultsFor(p);
        settings.kickClearanceTicks = 0;
        applyConstraints(p, settings);
    };
    Pattern locked = build();
    locked.voices[1].notes[0].lock.rhythm = true;
    run(locked);
    for (const Note& n : locked.voices[1].notes) {
        CHECK(n.startTick == 960); // the locked rhythm stays where it is
    }
    Pattern pitchLocked = build();
    pitchLocked.voices[1].notes[1].lock.pitch = true;
    run(pitchLocked);
    for (const Note& n : pitchLocked.voices[1].notes) {
        CHECK(n.startTick == 960);
    }
    Pattern taken = build();
    add(taken, 1, 82, 1200, 240); // the next step is used
    run(taken);
    int atStep = 0;
    for (const Note& n : taken.voices[1].notes) {
        atStep += n.startTick == 1200 ? 1 : 0;
    }
    CHECK(atStep <= 1); // only the note that was there: the stab did not move in
}

TEST_CASE("generated memory stabs keep every rule except the scale", "[chord-memory][series]") {
    for (const std::string name : {"peak_time", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            GenerationRequest request = stabsRequest(seed);
            request.lengthBars = seed % 2 == 0 ? 2 : 4;
            const Pattern p = generateCandidate(style, request, seed);
            INFO(name << " seed " << seed);
            CHECK(validatePattern(p).empty());
            CHECK(mm::series::violations(p, style).empty());
        }
    }
}

TEST_CASE("the constraint layer is idempotent for memory stabs", "[chord-memory][constraints]") {
    for (const std::string name : {"peak_time", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 80; ++seed) {
            const Pattern p = generateCandidate(style, stabsRequest(seed), seed);
            Pattern again = p;
            applyConstraints(again, constraintSettingsFor(again, style));
            INFO(name << " seed " << seed);
            CHECK(again == p);
        }
    }
}
