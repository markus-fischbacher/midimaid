#include "PatternFixtures.h"
#include "core/OutputStage.h"
#include "core/PlaybackRender.h"
#include "core/Probability.h"

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

Note& add(Pattern& p, size_t voice, int pitch, uint32_t start, uint32_t length) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

PatternNote pn(uint32_t id, uint32_t start, uint8_t chance = 100, uint8_t a = 1, uint8_t b = 1) {
    PatternNote note{start, 240, 1, 60, 100};
    note.noteId = id;
    note.chance = chance;
    note.condA = a;
    note.condB = b;
    return note;
}

} // namespace

TEST_CASE("the decision of a note is a pure function of seed, id and pass", "[probability]") {
    CHECK(passHash(1, 2, 3) == passHash(1, 2, 3));
    CHECK(passHash(1, 2, 3) != passHash(2, 2, 3));
    CHECK(passHash(1, 2, 3) != passHash(1, 3, 3));
    CHECK(passHash(1, 2, 3) != passHash(1, 2, 4));
}

TEST_CASE("100 % and 1:1 always sound, 0 % never", "[probability]") {
    for (uint64_t pass = 0; pass < 50; ++pass) {
        CHECK(soundsInPass(100, 1, 1, 77, 5, pass));
        CHECK_FALSE(soundsInPass(0, 1, 1, 77, 5, pass));
    }
}

TEST_CASE("a condition A:B plays in the A-th of every B passes", "[probability]") {
    for (uint64_t pass = 0; pass < 48; ++pass) {
        CHECK(soundsInPass(100, 2, 4, 1, 1, pass) == (pass % 4 == 1));
        CHECK(soundsInPass(100, 4, 4, 1, 1, pass) == (pass % 4 == 3));
        CHECK(soundsInPass(100, 3, 8, 1, 1, pass) == (pass % 8 == 2));
        CHECK(soundsInPass(100, 1, 3, 1, 1, pass) == (pass % 3 == 0));
        CHECK(soundsInPass(100, 8, 8, 1, 1, pass) == (pass % 8 == 7));
    }
    // out of range values are brought in: A below 1 is 1, A above B is B
    for (uint64_t pass = 0; pass < 24; ++pass) {
        CHECK(soundsInPass(100, 0, 4, 1, 1, pass) == soundsInPass(100, 1, 4, 1, 1, pass));
        CHECK(soundsInPass(100, 9, 4, 1, 1, pass) == soundsInPass(100, 4, 4, 1, 1, pass));
    }
}

TEST_CASE("the chance and the condition both have to hold", "[probability]") {
    int sounded = 0;
    for (uint64_t pass = 0; pass < 4000; ++pass) {
        const bool plays = soundsInPass(50, 1, 2, 9, 3, pass);
        CHECK((!plays || pass % 2 == 0)); // never in a pass the condition forbids
        sounded += plays ? 1 : 0;
    }
    CHECK(sounded > 800); // half of the half
    CHECK(sounded < 1200);
}

TEST_CASE("the chance is kept over many passes and notes", "[probability]") {
    for (const uint8_t chance : {25, 50, 75}) {
        int sounded = 0;
        const int total = 20 * 400;
        for (uint32_t id = 1; id <= 20; ++id) {
            for (uint64_t pass = 0; pass < 400; ++pass) {
                sounded += soundsInPass(chance, 1, 1, 31, id, pass) ? 1 : 0;
            }
        }
        INFO("chance " << int(chance));
        CHECK(sounded > total * (chance - 4) / 100);
        CHECK(sounded < total * (chance + 4) / 100);
    }
}

TEST_CASE("a note sounds when the hash modulo 100 is below its chance", "[probability]") {
    // the rule is part of the format: a project must sound the same in every version
    for (const uint8_t chance : {1, 33, 50, 99}) {
        for (uint64_t pass = 0; pass < 600; ++pass) {
            const bool expected = passHash(21, 8, pass) % 100 < chance;
            CHECK(soundsInPass(chance, 1, 1, 21, 8, pass) == expected);
        }
    }
}

TEST_CASE("an export covers the passes of every condition, within the bar limit", "[probability]") {
    const uint32_t oneBar = 3840;
    CHECK(exportPassCount({pn(1, 0)}, oneBar) == 1);
    CHECK(exportPassCount({pn(1, 0, 60)}, oneBar) == 4); // a chance only: a few draws
    CHECK(exportPassCount({pn(1, 0, 100, 2, 4)}, oneBar) == 4);
    CHECK(exportPassCount({pn(1, 0, 100, 1, 2), pn(2, 240, 100, 2, 3)}, oneBar) == 6);
    CHECK(exportPassCount({pn(1, 0, 100, 1, 8), pn(2, 240, 100, 1, 3)}, oneBar) == 24);
    CHECK(exportPassCount({pn(1, 0, 100, 1, 8), pn(2, 240, 100, 1, 7)}, oneBar) == 56); // 56 bars of one bar
    CHECK(exportPassCount({pn(1, 0, 100, 1, 8), pn(2, 240, 100, 1, 7), pn(3, 480, 100, 1, 5)}, oneBar) == 64); // limited
    CHECK(exportPassCount({pn(1, 0, 100, 1, 8)}, 4 * oneBar) == 8);
    CHECK(exportPassCount({pn(1, 0, 100, 1, 8), pn(2, 240, 100, 1, 3)}, 4 * oneBar) == 16); // 24 would be 96 bars
    CHECK(exportPassCount({pn(1, 0, 100, 1, 8)}, 16 * oneBar, 32) == 2);
    CHECK(exportPassCount({}, oneBar) == 1);
}

TEST_CASE("resolved passes unroll the pattern with the decisions made", "[probability]") {
    const std::vector<PatternNote> notes{pn(1, 0), pn(2, 960, 100, 2, 2), pn(3, 1920, 0)};
    const auto result = resolvePasses(notes, 3840, 5, 4);
    // note 1 four times, note 2 in passes 1 and 3, note 3 never
    REQUIRE(result.size() == 6);
    std::vector<uint32_t> starts;
    for (const auto& n : result) {
        starts.push_back(n.startTick);
        CHECK(n.chance == 100); // the result is plain
        CHECK(n.condA == 1);
        CHECK(n.condB == 1);
    }
    CHECK(starts == std::vector<uint32_t>{0, 3840, 3840 + 960, 7680, 11520, 11520 + 960});
    CHECK(resolvePasses(notes, 3840, 5, 0).empty());
    // everything sure: one pass is the pattern itself
    const std::vector<PatternNote> sure{pn(1, 0), pn(2, 480)};
    CHECK(resolvePasses(sure, 3840, 1, 1).size() == 2);
}

TEST_CASE("a resolved slide ends plain when its target stays out of the pass", "[probability]") {
    PatternNote slide{0, 480, 1, 40, 100};
    slide.noteId = 1;
    slide.slideTargetId = 2;
    slide.plainLengthTicks = 240;
    const PatternNote target = pn(2, 480, 100, 2, 2);
    const auto result = resolvePasses({slide, target}, 3840, 1, 2);
    std::vector<uint32_t> lengths;
    for (const auto& n : result) {
        if (n.pitch == 40) {
            lengths.push_back(n.lengthTicks);
            CHECK(n.slideTargetId == 0);
        }
    }
    CHECK(lengths == std::vector<uint32_t>{240, 480}); // pass 0: target out; pass 1: it plays, the slide stays
}

TEST_CASE("the output stage carries probability, conditions and the slide target to the playback notes", "[probability][output]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makeEmptyPattern(1, "peak_time");
    p.info.seed = 4;
    p.info.winnerSeed = 9;
    Note& first = add(p, 1, 60, 0, 240);
    first.slide = true;
    first.chance = 70;
    Note& second = add(p, 1, 67, 480, 240);
    second.condA = 2;
    second.condB = 4;
    const auto rendered = renderVoiceForPlayback(p, style, 1);
    REQUIRE(rendered.notes.size() == 2);
    CHECK(rendered.seed == (uint64_t{9} ^ uint64_t{4}));
    const auto& a = rendered.notes[0];
    const auto& b = rendered.notes[1];
    CHECK(a.noteId == p.voices[1].notes[0].id);
    CHECK(a.chance == 70);
    CHECK(a.condB == 1);
    CHECK(a.slideTargetId == p.voices[1].notes[1].id);
    CHECK(a.lengthTicks > 240);        // slid: reaches into the next note
    CHECK(a.plainLengthTicks == 240);  // plain: its own length
    CHECK(b.chance == 100);
    CHECK(b.condA == 2);
    CHECK(b.condB == 4);
    CHECK(b.slideTargetId == 0);
}

TEST_CASE("the parts of a ratchet carry the values of their note", "[probability][output]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makeEmptyPattern(1, "peak_time");
    Note& n = add(p, 1, 60, 480, 240);
    n.ratchet = 3;
    n.chance = 40;
    n.condA = 3;
    n.condB = 8;
    const auto rendered = renderVoiceForPlayback(p, style, 1);
    REQUIRE(rendered.notes.size() == 3);
    for (const auto& part : rendered.notes) {
        CHECK(part.noteId == p.voices[1].notes[0].id);
        CHECK(part.chance == 40);
        CHECK(part.condA == 3);
        CHECK(part.condB == 8);
    }
}

TEST_CASE("an export without chances and conditions is the notes themselves", "[probability]") {
    const std::vector<PatternNote> notes{pn(1, 0), pn(2, 480)};
    uint32_t length = 3840;
    CHECK(notesForExport(notes, length, 3).size() == 2);
    CHECK(length == 3840);
}

TEST_CASE("an export with conditions holds the passes and grows the length", "[probability]") {
    const std::vector<PatternNote> notes{pn(1, 0), pn(2, 480, 100, 2, 3)};
    uint32_t length = 3840;
    const auto result = notesForExport(notes, length, 3);
    CHECK(length == 3 * 3840);
    REQUIRE(result.size() == 4); // note 1 three times, note 2 once (the second of three passes)
    CHECK(result[2].startTick == 3840 + 480);
}

TEST_CASE("an export with a chance only covers four passes", "[probability]") {
    const std::vector<PatternNote> notes{pn(1, 0, 50)};
    uint32_t length = 3840;
    const auto result = notesForExport(notes, length, 11);
    CHECK(length == 4 * 3840);
    CHECK(result.size() <= 4);
    for (const auto& n : result) {
        CHECK(n.chance == 100);
    }
}
