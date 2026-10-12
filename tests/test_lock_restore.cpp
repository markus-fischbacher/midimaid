#include "core/LockRestore.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
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

Note make(uint32_t id, int pitch, uint32_t start, uint32_t length, int velocity = 100, bool accent = false) {
    Note note;
    note.id = id;
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    note.velocity = static_cast<uint8_t>(velocity);
    note.accent = accent;
    return note;
}

/// Four old notes and five fresh ones with different everything.
std::vector<Note> oldNotes() {
    return {make(1, 40, 0, 240, 90), make(2, 43, 480, 240, 80), make(3, 45, 960, 240, 70, true), make(4, 47, 1440, 240, 60)};
}

std::vector<Note> freshNotes() {
    return {make(11, 50, 240, 120, 110), make(12, 52, 720, 120, 111), make(13, 54, 1200, 120, 112),
            make(14, 56, 1680, 120, 113), make(15, 58, 2160, 120, 114)};
}

std::vector<int> pitches(const std::vector<Note>& notes) {
    std::vector<int> result;
    for (const Note& n : notes) {
        result.push_back(n.pitch);
    }
    return result;
}

std::vector<uint32_t> starts(const std::vector<Note>& notes) {
    std::vector<uint32_t> result;
    for (const Note& n : notes) {
        result.push_back(n.startTick);
    }
    return result;
}

} // namespace

TEST_CASE("no locks mean the fresh notes", "[lock-restore]") {
    CHECK(restoreLocks(oldNotes(), freshNotes(), {}) == freshNotes());
    CHECK(restoreLocks({}, freshNotes(), {true, true, true}) == freshNotes()); // nothing to restore from
}

TEST_CASE("a locked pitch sequence runs through the new rhythm", "[lock-restore]") {
    const auto result = restoreLocks(oldNotes(), freshNotes(), {true, false, false});
    CHECK(starts(result) == starts(freshNotes()));
    CHECK(pitches(result) == std::vector<int>{40, 43, 45, 47, 40}); // the old pitches in turn
    CHECK(result[0].velocity == 110); // velocity is not locked
    CHECK(result[0].lengthTicks == 120);
}

TEST_CASE("a locked rhythm keeps positions and lengths and takes new pitches and velocities", "[lock-restore]") {
    const auto result = restoreLocks(oldNotes(), freshNotes(), {false, true, false});
    CHECK(starts(result) == starts(oldNotes()));
    CHECK(pitches(result) == std::vector<int>{50, 52, 54, 56});
    CHECK(result[2].velocity == 112);
    CHECK_FALSE(result[2].accent); // the accent goes with the velocity
    CHECK(result[0].lengthTicks == 240);
    CHECK(result[0].id == 1); // the notes are the old ones
}

TEST_CASE("a locked velocity sequence keeps velocities and accents", "[lock-restore]") {
    const auto result = restoreLocks(oldNotes(), freshNotes(), {false, false, true});
    CHECK(starts(result) == starts(freshNotes()));
    CHECK(pitches(result) == pitches(freshNotes()));
    CHECK(result[0].velocity == 90);
    CHECK(result[2].velocity == 70);
    CHECK(result[2].accent);
    CHECK(result[4].velocity == 90); // in turn
}

TEST_CASE("locked pitch and rhythm keep the old notes with new velocities", "[lock-restore]") {
    const auto result = restoreLocks(oldNotes(), freshNotes(), {true, true, false});
    CHECK(starts(result) == starts(oldNotes()));
    CHECK(pitches(result) == pitches(oldNotes()));
    CHECK(result[1].velocity == 111);
}

TEST_CASE("a note locked in its rhythm stays and lets the fresh notes there give way", "[lock-restore]") {
    auto old = oldNotes();
    old[1].lock.rhythm = true; // the note at 480
    auto fresh = freshNotes();
    fresh.push_back(make(16, 60, 480, 120, 99)); // a fresh note at the same start
    const auto result = restoreLocks(old, fresh, {});
    size_t at480 = 0;
    for (const Note& n : result) {
        if (n.startTick == 480) {
            ++at480;
            CHECK(n.lengthTicks == 240); // the old length
            CHECK(n.pitch == 60);        // pitch and velocity are free: they come from the fresh note that stood there
            CHECK(n.velocity == 99);
            CHECK(n.lock.rhythm);
        }
    }
    CHECK(at480 == 1);
    CHECK(result.size() == fresh.size()); // the fresh note was replaced, the others stay
}

TEST_CASE("a note locked in all dimensions comes back as it was", "[lock-restore]") {
    auto old = oldNotes();
    old[2].lock = {true, true, true};
    const auto result = restoreLocks(old, freshNotes(), {});
    bool found = false;
    for (const Note& n : result) {
        if (n.id == 3) {
            found = true;
            CHECK(n == old[2]);
        }
    }
    CHECK(found);
}

TEST_CASE("a note locked in pitch or velocity only gives its value to the nearest fresh note", "[lock-restore]") {
    auto old = oldNotes();
    old[0].lock.pitch = true;    // pitch 40 at 0: the nearest fresh note starts at 240
    old[3].lock.velocity = true; // velocity 60 (no accent) at 1440: nearest fresh at 1200 or 1680 (1440 - 1200 = 240)
    const auto result = restoreLocks(old, freshNotes(), {});
    CHECK(starts(result) == starts(freshNotes()));
    CHECK(result[0].pitch == 40);
    CHECK(result[0].lock.pitch);
    CHECK(result[2].velocity == 60);
    CHECK(result[2].lock.velocity);
    CHECK(result[1].pitch == 52); // the others are fresh
    CHECK(result[1].velocity == 111);
}

TEST_CASE("every locked note gets its own fresh note", "[lock-restore]") {
    auto old = oldNotes();
    old[0].lock.pitch = true;
    old[1].lock.pitch = true; // both would pick the fresh note at 240
    const auto result = restoreLocks(old, freshNotes(), {});
    CHECK(result[0].pitch == 40);
    CHECK(result[1].pitch == 43);
}

TEST_CASE("hasAnyLock looks at the voice and at the notes", "[lock-restore]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    CHECK_FALSE(hasAnyLock(p));
    p.voices[1].lock.velocity = true;
    CHECK(hasAnyLock(p.voices[1]));
    CHECK(hasAnyLock(p));
    p.voices[1].lock = {};
    p.voices[0].notes.push_back(make(1, 33, 0, 240));
    CHECK_FALSE(hasAnyLock(p));
    p.voices[0].notes[0].lock.rhythm = true;
    CHECK(hasAnyLock(p.voices[0]));
    CHECK(hasAnyLock(p));
}

TEST_CASE("setVoiceLock and setNoteLock change one dimension", "[lock-restore][edit]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    p.voices[1].notes = {make(1, 60, 0, 240), make(2, 62, 240, 240)};
    CHECK(setVoiceLock(p, 1, LockDimension::Rhythm, true) == 1);
    CHECK(p.voices[1].lock == LockFlags{false, true, false});
    CHECK(setVoiceLock(p, 1, LockDimension::Rhythm, true) == 0);
    CHECK(setVoiceLock(p, 1, LockDimension::Pitch, true) == 1);
    CHECK(setVoiceLock(p, 1, LockDimension::Velocity, true) == 1);
    CHECK(isVoiceLocked(p.voices[1]));
    CHECK(setVoiceLock(p, 1, LockDimension::Pitch, false) == 1);
    CHECK_FALSE(isVoiceLocked(p.voices[1]));
    CHECK(setVoiceLock(p, 9, LockDimension::Pitch, true) == 0);

    const std::vector<uint32_t> ids{2};
    CHECK(setNoteLock(p, 1, ids, LockDimension::Velocity, true) == 1);
    CHECK(p.voices[1].notes[1].lock == LockFlags{false, false, true});
    CHECK(p.voices[1].notes[0].lock == LockFlags{});
    CHECK(setNoteLock(p, 1, ids, LockDimension::Velocity, true) == 0);
    CHECK(setNoteLock(p, 1, ids, LockDimension::Velocity, false) == 1);
    CHECK(setNoteLock(p, 9, ids, LockDimension::Pitch, true) == 0);
}

TEST_CASE("generating around a voice with a locked rhythm keeps the rhythm and draws new tones", "[lock-restore][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        GenerationRequest request;
        request.lengthBars = 4;
        request.seed = seed;
        Pattern current = generateCandidate(style, request, seed);
        REQUIRE_FALSE(current.voices[1].notes.empty());
        current.voices[1].lock.rhythm = true;
        const auto result = generatePatternAroundLocks(style, request, current);
        REQUIRE(result.success);
        INFO("seed " << seed);
        // the constraint layer leaves a voice with a locked rhythm in place
        CHECK(starts(result.pattern.voices[1].notes) == starts(current.voices[1].notes));
        CHECK(validatePattern(result.pattern).empty());
        CHECK(result.pattern.voices[1].lock == LockFlags{false, true, false});
    }
}

TEST_CASE("generating around a voice with a locked pitch sequence keeps the tones in their order", "[lock-restore][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    int kept = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        GenerationRequest request;
        request.lengthBars = 4;
        request.seed = seed;
        request.melodyArchetype = "pluck_seq";
        Pattern current = generateCandidate(style, request, seed);
        current.voices[1].lock.pitch = true;
        const auto result = generatePatternAroundLocks(style, request, current);
        REQUIRE(result.success);
        std::set<int> before;
        for (const Note& n : current.voices[1].notes) {
            before.insert(n.pitch);
        }
        std::set<int> after;
        for (const Note& n : result.pattern.voices[1].notes) {
            after.insert(n.pitch);
        }
        // a pitch lock keeps every pitch of the line (the constraint layer does not touch them)
        kept += std::includes(before.begin(), before.end(), after.begin(), after.end()) ? 1 : 0;
    }
    CHECK(kept >= 10);
}

TEST_CASE("renewing a voice and a phrase honour the locked dimensions", "[lock-restore][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request;
    request.lengthBars = 8;
    request.seed = 3;
    Phrase a;
    a.lengthBars = 4;
    Phrase b;
    b.startBar = 4;
    b.lengthBars = 4;
    b.role = PhraseRole::Variation;
    request.formPlan = std::vector<Phrase>{a, b};
    Pattern pattern = generateCandidate(style, request, 3);
    pattern.voices[1].lock.rhythm = true;
    const auto rhythm = starts(pattern.voices[1].notes);
    Pattern renewed = pattern;
    REQUIRE(regenerateVoice(renewed, 1, style, request, 77));
    CHECK(starts(renewed.voices[1].notes) == rhythm);
    Pattern phrased = pattern;
    REQUIRE(regeneratePhrase(phrased, 1, style, request, 78));
    CHECK(starts(phrased.voices[1].notes) == rhythm);
}
