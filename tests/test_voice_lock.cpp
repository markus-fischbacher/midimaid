#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>
#include <string>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;
using namespace mm::fixtures;

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

GenerationRequest requestFor(uint32_t bars, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    return request;
}

Pattern lockedSource(const StyleProfile& style, uint32_t bars, uint64_t seed, size_t lockedVoice) {
    const auto result = generatePattern(style, requestFor(bars, seed));
    REQUIRE(result.success);
    Pattern pattern = result.pattern;
    REQUIRE(setVoiceLocked(pattern, lockedVoice, true) == 1);
    return pattern;
}

} // namespace

TEST_CASE("a voice is locked when all three dimensions are", "[voice-lock]") {
    Track track;
    CHECK_FALSE(isVoiceLocked(track));
    track.lock.pitch = true;
    CHECK_FALSE(isVoiceLocked(track)); // a partial lock (v1.1) does not count in v1.0
    track.lock.rhythm = track.lock.velocity = true;
    CHECK(isVoiceLocked(track));
    Pattern pattern = makeEmptyPattern(1, "x");
    CHECK_FALSE(hasLockedVoice(pattern));
    pattern.voices[1].lock = {true, true, true};
    CHECK(hasLockedVoice(pattern));
}

TEST_CASE("setVoiceLocked sets and clears all three flags and reports a change", "[voice-lock]") {
    Pattern pattern = makeEmptyPattern(1, "x");
    pattern.info.source = "algorithm";
    CHECK(setVoiceLocked(pattern, 0, true) == 1);
    CHECK(pattern.voices[0].lock == LockFlags{true, true, true});
    CHECK(setVoiceLocked(pattern, 0, true) == 0);
    CHECK(pattern.info.source == "algorithm"); // locking is no note edit
    CHECK_FALSE(isVoiceLocked(pattern.voices[1]));
    CHECK(setVoiceLocked(pattern, 0, false) == 1);
    CHECK(pattern.voices[0].lock == LockFlags{});
    CHECK(setVoiceLocked(pattern, 0, false) == 0);
    pattern.voices[1].lock.pitch = true; // partial: unlocking clears it
    CHECK(setVoiceLocked(pattern, 1, false) == 1);
    CHECK(pattern.voices[1].lock == LockFlags{});
    CHECK(setVoiceLocked(pattern, 5, true) == 0);
}

TEST_CASE("the locks of single notes stay when the voice is locked and unlocked", "[voice-lock]") {
    Pattern pattern = makeEmptyPattern(1, "x");
    const auto id = addNote(pattern, 0, 45, 0, 240, 100);
    pattern.voices[0].notes[0].lock.pitch = true;
    setVoiceLocked(pattern, 0, true);
    setVoiceLocked(pattern, 0, false);
    CHECK(pattern.voices[0].notes[0].id == id);
    CHECK(pattern.voices[0].notes[0].lock == LockFlags{true, false, false});
}

TEST_CASE("regenerateVoice and regeneratePhrase leave a locked voice alone", "[voice-lock]") {
    const StyleProfile style = loadShipped("peak_time");
    const GenerationRequest request = requestFor(4, 3);
    Pattern pattern = generateCandidate(style, request, 11);
    const Pattern before = pattern;
    setVoiceLocked(pattern, 0, true);
    const Pattern locked = pattern;
    CHECK_FALSE(regenerateVoice(pattern, 0, style, request, 99));
    CHECK(pattern == locked);
    REQUIRE(regenerateVoice(pattern, 1, style, request, 99));
    CHECK(pattern.voices[0] == locked.voices[0]);
    CHECK_FALSE(pattern.voices[1] == before.voices[1]);

    GenerationRequest planned = requestFor(8, 4);
    Pattern long8 = generateCandidate(style, planned, 12);
    REQUIRE(long8.phrases.size() >= 1);
    setVoiceLocked(long8, 0, true);
    const auto bass = long8.voices[0];
    for (size_t i = 0; i < long8.phrases.size(); ++i) {
        regeneratePhrase(long8, i, style, planned, 30 + i);
        CHECK(long8.voices[0] == bass);
    }
}

TEST_CASE("generating around a locked voice keeps it exactly and the context with it", "[voice-lock][generator]") {
    for (const char* styleName : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile style = loadShipped(styleName);
        for (const size_t lockedVoice : {size_t{0}, size_t{1}}) {
            const Pattern current = lockedSource(style, 4, 21, lockedVoice);
            GenerationRequest request = requestFor(2, 77); // another length and key than the slot has: ignored
            request.root = 3;
            request.scaleId = "dorian";
            const auto result = generatePatternAroundLocks(style, request, current);
            INFO(styleName << " locked voice " << lockedVoice);
            REQUIRE(result.success);
            const Pattern& next = result.pattern;
            CHECK(next.voices[lockedVoice] == current.voices[lockedVoice]); // notes, ids, archetype, lock
            CHECK(next.context == current.context);
            CHECK(next.lengthBars == current.lengthBars);
            CHECK(next.phrases == current.phrases);
            CHECK(next.kickGridId == current.kickGridId);
            CHECK(validatePattern(next).empty());
            const size_t other = 1 - lockedVoice;
            CHECK_FALSE(next.voices[other].notes.empty());
            CHECK_FALSE(next.voices[other].notes == current.voices[other].notes);
            CHECK(next.info.seed == 77);
            CHECK(next.nextNoteId >= current.nextNoteId);
            CHECK(next.qualityScore > 0);
        }
    }
}

TEST_CASE("generating around locks is deterministic and the seed makes the difference", "[voice-lock][generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const Pattern current = lockedSource(style, 4, 5, 0);
    const auto a = generatePatternAroundLocks(style, requestFor(4, 1), current);
    const auto b = generatePatternAroundLocks(style, requestFor(4, 1), current);
    const auto c = generatePatternAroundLocks(style, requestFor(4, 2), current);
    REQUIRE((a.success && b.success && c.success));
    CHECK(a.pattern == b.pattern);
    CHECK_FALSE(a.pattern.voices[1].notes == c.pattern.voices[1].notes);
}

TEST_CASE("generating around a locked voice works for patterns with several phrases", "[voice-lock][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    for (const uint32_t bars : {8u, 16u}) {
        for (const size_t lockedVoice : {size_t{0}, size_t{1}}) {
            const Pattern current = lockedSource(style, bars, 9, lockedVoice);
            REQUIRE(current.phrases.size() > 1);
            const auto result = generatePatternAroundLocks(style, requestFor(bars, 4), current);
            INFO(bars << " bars, locked voice " << lockedVoice);
            REQUIRE(result.success);
            CHECK(result.pattern.voices[lockedVoice] == current.voices[lockedVoice]);
            CHECK(result.pattern.phrases == current.phrases);
            CHECK(validatePattern(result.pattern).empty());
            const size_t other = 1 - lockedVoice;
            CHECK_FALSE(result.pattern.voices[other].notes.empty());
            CHECK_FALSE(result.pattern.voices[other].notes == current.voices[other].notes);
        }
    }
}

TEST_CASE("a locked voice without archetype (drawn or imported) is kept", "[voice-lock][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern current = lockedSource(style, 4, 6, 0);
    current.voices[0].archetypeId.clear();
    current.voices[0].archetypeAuto = true;
    const auto result = generatePatternAroundLocks(style, requestFor(4, 8), current);
    REQUIRE(result.success);
    CHECK(result.pattern.voices[0] == current.voices[0]);
}

TEST_CASE("with every voice locked there is nothing to generate", "[voice-lock][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern current = lockedSource(style, 4, 6, 0);
    setVoiceLocked(current, 1, true);
    const auto result = generatePatternAroundLocks(style, requestFor(4, 8), current);
    CHECK_FALSE(result.success);
}

TEST_CASE("the constraint layer does not change a locked voice that breaks its rules", "[voice-lock][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern current = lockedSource(style, 4, 6, 1); // melody locked
    // A hand-drawn note off the scale and far above the range stays as drawn.
    const auto id = addNote(current, 1, 121, 3 * 960, 120, 100);
    REQUIRE(id != 0);
    const auto result = generatePatternAroundLocks(style, requestFor(4, 3), current);
    REQUIRE(result.success);
    CHECK(result.pattern.voices[1] == current.voices[1]);
}

TEST_CASE("series: the locked voice is the same for many seeds", "[voice-lock][series]") {
    const StyleProfile style = loadShipped("hard_industrial");
    const Pattern current = lockedSource(style, 2, 14, 0);
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        const auto result = generatePatternAroundLocks(style, requestFor(2, seed), current);
        INFO("seed " << seed);
        REQUIRE(result.success);
        CHECK(result.pattern.voices[0] == current.voices[0]);
        CHECK(validatePattern(result.pattern).empty());
    }
}
