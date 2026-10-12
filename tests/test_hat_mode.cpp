#include "core/Constraints.h"
#include "core/DrumReference.h"
#include "core/HatMode.h"
#include "core/PatternValidation.h"

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

Note& add(Pattern& p, size_t voice, int pitch, uint32_t step, uint32_t length = 180) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = step * 240;
    note.lengthTicks = length;
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

/// A reference with hats on the off-beat 8ths (steps 2, 6, 10, 14).
RhythmReference offbeatHats(uint8_t bars = 1) {
    RhythmReference ref;
    ref.bars = bars;
    for (uint32_t bar = 0; bar < bars; ++bar) {
        for (const uint32_t step : {2u, 6u, 10u, 14u}) {
            ref.hatSteps.set(bar * 16 + step);
        }
    }
    ref.kickSteps.set(0);
    return ref;
}

Pattern melodyAt(std::initializer_list<uint32_t> steps, uint32_t bars = 1) {
    Pattern p = makeEmptyPattern(bars, "peak_time");
    p.rhythmRef = offbeatHats(1);
    for (const uint32_t step : steps) {
        add(p, 1, 60, step);
    }
    return p;
}

std::vector<uint32_t> stepsOf(const Pattern& p, size_t voice = 1) {
    std::vector<uint32_t> steps;
    for (const Note& note : p.voices[voice].notes) {
        steps.push_back(note.startTick / 240);
    }
    return steps;
}

using Steps = std::vector<uint32_t>;

} // namespace

TEST_CASE("the hat modes have texts that parse back", "[hatmode]") {
    for (const HatMode mode : {HatMode::Free, HatMode::With, HatMode::Against}) {
        CHECK(parseHatMode(toString(mode)) == mode);
    }
    CHECK(toString(HatMode::With) == "with");
    CHECK(toString(HatMode::Against) == "against");
    CHECK(parseHatMode("") == HatMode::Free);
    CHECK(parseHatMode("sideways") == HatMode::Free);
}

TEST_CASE("with hats: notes next to a hat step move onto it, the beats stay", "[hatmode]") {
    Pattern p = melodyAt({0, 1, 3, 4, 5, 8, 9});
    applyHatMode(p, HatMode::With);
    // 1 -> 2; 3 would go to 2 (taken) or 4 (not a hat): stays; 5 -> 6; 9 -> 10; beats 0, 4, 8 stay
    CHECK(stepsOf(p) == Steps{0, 2, 3, 4, 6, 8, 10});
}

TEST_CASE("against hats: notes on a hat step move into the gap, the nearer side first", "[hatmode]") {
    Pattern p = melodyAt({2, 6, 10, 14, 5});
    applyHatMode(p, HatMode::Against);
    // 2 -> 1 (the earlier side on a tie); 6 -> 5 is taken: 7; 10 -> 9; 14 -> 13; the 5 was a gap already
    CHECK(stepsOf(p) == Steps{1, 5, 7, 9, 13});
    Pattern q = melodyAt({1, 2});
    applyHatMode(q, HatMode::Against);
    CHECK(stepsOf(q) == Steps{1, 3}); // 1 is taken, so the later side
}

TEST_CASE("free mode, a missing reference and a reference without hats change nothing", "[hatmode]") {
    const Pattern original = melodyAt({1, 2, 3, 5, 6, 9, 10});
    Pattern a = original;
    applyHatMode(a, HatMode::Free);
    CHECK(a == original);
    Pattern b = original;
    b.rhythmRef.reset();
    const Pattern bBefore = b;
    applyHatMode(b, HatMode::With);
    CHECK(b == bBefore);
    Pattern c = original;
    c.rhythmRef->hatSteps.reset();
    const Pattern cBefore = c;
    applyHatMode(c, HatMode::Against);
    CHECK(c == cBefore);
}

TEST_CASE("the beats anchor the line in both modes", "[hatmode]") {
    Pattern against = melodyAt({4, 8});
    against.rhythmRef->hatSteps.reset();
    for (const uint32_t step : {0u, 4u, 8u, 12u}) {
        against.rhythmRef->hatSteps.set(step); // hats on the beats: a note there would like to leave
    }
    applyHatMode(against, HatMode::Against);
    CHECK(stepsOf(against) == Steps{4, 8});
    Pattern with = melodyAt({4, 8});
    with.rhythmRef->hatSteps.reset();
    for (const uint32_t step : {3u, 9u}) {
        with.rhythmRef->hatSteps.set(step); // hats next to the beats
    }
    applyHatMode(with, HatMode::With);
    CHECK(stepsOf(with) == Steps{4, 8});
}

TEST_CASE("chords move together, other voices, slides and locks stay", "[hatmode]") {
    Pattern p = melodyAt({1});
    add(p, 1, 64, 1); // a chord on step 1
    add(p, 0, 36, 1); // the bass
    Pattern q = p;
    applyHatMode(q, HatMode::With);
    CHECK(stepsOf(q) == Steps{2, 2});
    CHECK(stepsOf(q, 0) == Steps{1});

    Pattern slide = melodyAt({1, 5});
    slide.voices[1].notes[0].slide = true;
    applyHatMode(slide, HatMode::With);
    CHECK(stepsOf(slide) == Steps{1, 6});

    Pattern noteLock = melodyAt({1, 5});
    noteLock.voices[1].notes[0].lock.rhythm = true;
    applyHatMode(noteLock, HatMode::With);
    CHECK(stepsOf(noteLock) == Steps{1, 6});

    Pattern voiceLock = melodyAt({1, 5});
    voiceLock.voices[1].lock.rhythm = true;
    applyHatMode(voiceLock, HatMode::With);
    CHECK(stepsOf(voiceLock) == Steps{1, 5});
}

TEST_CASE("notes off the 16th grid and moves out of the pattern are left alone", "[hatmode]") {
    Pattern p = melodyAt({5});
    p.voices[1].notes[0].startTick += 7; // off the grid
    applyHatMode(p, HatMode::With);
    CHECK(p.voices[1].notes[0].startTick == 5 * 240 + 7);
    // a step 15 note whose neighbour would be past the end: the hat at 14 is reachable, a hat at 16 would not be
    Pattern q = melodyAt({15});
    applyHatMode(q, HatMode::With);
    CHECK(stepsOf(q) == Steps{14});
    Pattern r = makeEmptyPattern(1, "peak_time");
    r.rhythmRef = RhythmReference{};
    r.rhythmRef->bars = 1;
    r.rhythmRef->hatSteps.set(0); // a hat at step 0 of the next pass only matters inside the pattern
    add(r, 1, 60, 15);
    applyHatMode(r, HatMode::With);
    CHECK(stepsOf(r) == Steps{15});
}

TEST_CASE("a reference of one bar repeats every bar", "[hatmode]") {
    Pattern p = melodyAt({1, 17, 33}, 3);
    applyHatMode(p, HatMode::With);
    CHECK(stepsOf(p) == Steps{2, 18, 34});
}

TEST_CASE("a reference of two bars repeats over longer patterns", "[hatmode]") {
    Pattern p = makeEmptyPattern(4, "peak_time");
    p.rhythmRef = offbeatHats(2);
    p.rhythmRef->hatSteps.reset(2); // bar 1 of the reference has the off-beat hats, bar 0 has none at step 2
    for (const uint32_t bar : {0u, 1u, 2u, 3u}) {
        add(p, 1, 60, bar * 16 + 1);
    }
    applyHatMode(p, HatMode::With);
    // step 1 of a bar can only move to step 2: it is a hat in the reference bars 1 and 3 (steps 18, 18 + 32 -> 18) only
    CHECK(stepsOf(p) == Steps{1, 18, 33, 50});
}

TEST_CASE("the reference applies the mode before the constraint layer", "[hatmode][drum-reference]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 1, 60, 1);
    add(p, 1, 62, 5);
    add(p, 1, 64, 9);
    applyRhythmReference(p, offbeatHats(), style, HatMode::With);
    CHECK(validatePattern(p).empty());
    CHECK(p.rhythmRef.has_value());
    CHECK(stepsOf(p) == Steps{2, 6, 10});
    Pattern q = makeEmptyPattern(1, "peak_time");
    add(q, 1, 60, 1);
    applyRhythmReference(q, offbeatHats(), style); // the default is free
    CHECK(stepsOf(q) == Steps{1});
}
