#include "core/Chance.h"
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

Note& add(Pattern& p, size_t voice, int pitch, uint32_t step, int velocity = 100, uint32_t length = 180) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = step * 240;
    note.lengthTicks = length;
    note.velocity = static_cast<uint8_t>(velocity);
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

/// A bass of 16ths: the off steps are quiet (ghost candidates), the on steps loud.
Pattern ghostBass(uint32_t bars = 4) {
    Pattern p = makeEmptyPattern(bars, "peak_time");
    for (uint32_t step = 0; step < bars * 16; ++step) {
        add(p, 0, 36, step, step % 2 == 1 ? 60 : 100);
    }
    return p;
}

size_t withChance(const Track& track) {
    size_t count = 0;
    for (const Note& n : track.notes) {
        count += n.chance < 100 || n.condB > 1 ? 1 : 0;
    }
    return count;
}

} // namespace

TEST_CASE("the budget rises with the energy and stays under 15 percent", "[chance]") {
    CHECK(chanceBudgetPct(0) == 4);
    CHECK(chanceBudgetPct(50) == 9);
    CHECK(chanceBudgetPct(100) == 14);
    CHECK(chanceBudgetPct(-5) == 4);
    CHECK(chanceBudgetPct(900) == 14);
}

TEST_CASE("Peak Time gives ghost notes of the bass a chance of 50 to 75 percent", "[chance]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern p = ghostBass();
        applyChances(p, 0, style, 100, seed, 0, 4);
        const size_t budget = 64 * static_cast<size_t>(chanceBudgetPct(100)) / 100;
        CHECK(withChance(p.voices[0]) == budget);
        for (const Note& n : p.voices[0].notes) {
            if (n.chance < 100) {
                CHECK(n.chance >= 50);
                CHECK(n.chance <= 75);
                CHECK(n.startTick % 480 == 240); // an off 16th
                CHECK(n.velocity < 80);          // a quiet one
                CHECK(n.condB == 1);
            }
        }
    }
}

TEST_CASE("the melody ornaments of Peak Time get 60 to 80 percent", "[chance]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = makeEmptyPattern(2, "peak_time");
    for (uint32_t step = 0; step < 32; ++step) {
        Note& n = add(p, 1, 64, step, 80);
        n.accent = step % 8 == 1; // accents are no ornaments
    }
    applyChances(p, 1, style, 60, 4, 0, 2);
    CHECK(withChance(p.voices[1]) > 0);
    for (const Note& n : p.voices[1].notes) {
        if (n.chance < 100) {
            CHECK(n.chance >= 60);
            CHECK(n.chance <= 80);
            CHECK_FALSE(n.accent);
            CHECK(n.startTick % 480 == 240);
        }
    }
}

TEST_CASE("Melodic Techno uses a condition 2:2 in answer phrases only", "[chance]") {
    const StyleProfile style = loadShipped("melodic_techno");
    Pattern p = makeEmptyPattern(8, "melodic_techno");
    p.phrases.clear();
    Phrase a;
    a.lengthBars = 4;
    Phrase b;
    b.startBar = 4;
    b.lengthBars = 4;
    b.role = PhraseRole::Answer;
    p.phrases = {a, b};
    for (uint32_t step = 0; step < 128; ++step) {
        add(p, 1, 64, step, 80);
    }
    applyChances(p, 1, style, 100, 8, 0, 8);
    size_t marked = 0;
    for (const Note& n : p.voices[1].notes) {
        if (n.condB > 1) {
            ++marked;
            CHECK(n.condA == 2);
            CHECK(n.condB == 2);
            CHECK(n.chance == 100);
            CHECK(n.startTick >= 4 * kTicksPerBar); // in the answer phrase
        }
        CHECK(n.chance == 100);
    }
    CHECK(marked > 0);
}

TEST_CASE("Hard/Industrial gives ratchets and sparse hits 40 to 70 percent", "[chance]") {
    const StyleProfile style = loadShipped("hard_industrial");
    Pattern p = makeEmptyPattern(4, "hard_industrial");
    for (uint32_t bar = 0; bar < 4; ++bar) {
        for (uint32_t s = 0; s < 4; ++s) {
            Note& n = add(p, 0, 31, bar * 16 + s * 4, 110);
            n.ratchet = 3;
        }
    }
    p.voices[1].archetypeId = "sparse_hits";
    for (uint32_t step = 0; step < 64; step += 3) {
        add(p, 1, 67, step, 100);
    }
    applyChances(p, 0, style, 100, 3, 0, 4);
    applyChances(p, 1, style, 100, 3, 0, 4);
    CHECK(withChance(p.voices[0]) > 0);
    CHECK(withChance(p.voices[1]) > 0);
    for (size_t v = 0; v < 2; ++v) {
        for (const Note& n : p.voices[v].notes) {
            if (n.chance < 100) {
                CHECK(n.chance >= 40);
                CHECK(n.chance <= 70);
                if (v == 0) {
                    CHECK(n.ratchet > 1);
                }
            }
        }
    }
}

TEST_CASE("notes with a chance, a condition or a locked rhythm are left alone", "[chance]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = ghostBass();
    for (Note& n : p.voices[0].notes) {
        if (n.startTick % 480 == 240) {
            n.chance = 33;
        }
    }
    const Pattern before = p;
    applyChances(p, 0, style, 100, 1, 0, 4);
    CHECK(p == before); // every candidate already has a chance
    Pattern locked = ghostBass();
    locked.voices[0].lock.rhythm = true;
    applyChances(locked, 0, style, 100, 1, 0, 4);
    CHECK(withChance(locked.voices[0]) == 0);
    Pattern noteLock = ghostBass();
    for (Note& n : noteLock.voices[0].notes) {
        n.lock.rhythm = true;
    }
    applyChances(noteLock, 0, style, 100, 1, 0, 4);
    CHECK(withChance(noteLock.voices[0]) == 0);
}

TEST_CASE("chances that are there already use up the budget", "[chance]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern p = ghostBass();
    size_t marked = 0;
    for (Note& n : p.voices[0].notes) {
        if (n.startTick % 480 == 0 && marked < 9) { // loud notes on the beat: no candidates, but a chance each
            n.chance = 70;
            ++marked;
        }
    }
    applyChances(p, 0, style, 100, 1, 0, 4); // the budget is 8 of 64
    size_t ghosts = 0;
    for (const Note& n : p.voices[0].notes) {
        ghosts += n.startTick % 480 == 240 && n.chance < 100 ? 1 : 0;
    }
    CHECK(ghosts == 0);
    Pattern some = ghostBass();
    marked = 0;
    for (Note& n : some.voices[0].notes) {
        if (n.startTick % 480 == 0 && marked < 5) {
            n.chance = 70;
            ++marked;
        }
    }
    applyChances(some, 0, style, 100, 1, 0, 4);
    ghosts = 0;
    for (const Note& n : some.voices[0].notes) {
        ghosts += n.startTick % 480 == 240 && n.chance < 100 ? 1 : 0;
    }
    CHECK(ghosts == 3); // 8 - 5
}

TEST_CASE("the draw is a function of the seed and limited to the bars given", "[chance]") {
    const StyleProfile style = loadShipped("peak_time");
    Pattern a = ghostBass();
    Pattern b = ghostBass();
    applyChances(a, 0, style, 80, 12, 0, 4);
    applyChances(b, 0, style, 80, 12, 0, 4);
    CHECK(a == b);
    Pattern c = ghostBass();
    applyChances(c, 0, style, 80, 13, 0, 4);
    CHECK(c != a);
    Pattern window = ghostBass();
    applyChances(window, 0, style, 100, 5, 1, 3);
    for (const Note& n : window.voices[0].notes) {
        if (n.chance < 100) {
            CHECK(n.startTick >= kTicksPerBar);
            CHECK(n.startTick < 3 * kTicksPerBar);
        }
    }
    Pattern none = ghostBass();
    applyChances(none, 9, style, 100, 5, 0, 4); // an unknown voice
    CHECK(none == ghostBass());
}

TEST_CASE("generated patterns carry few chances and stay valid", "[chance][generator]") {
    int patternsWith = 0;
    for (const std::string name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            GenerationRequest request;
            request.lengthBars = seed % 2 == 0 ? 8 : 4;
            request.seed = seed;
            request.settings.energyPct = 90;
            const Pattern p = generateCandidate(style, request, seed);
            INFO(name << " seed " << seed);
            CHECK(validatePattern(p).empty());
            bool any = false;
            for (const Track& track : p.voices) {
                const size_t marked = withChance(track);
                any = any || marked > 0;
                CHECK(marked * 100 <= track.notes.size() * 15); // the 15 % of STYLES.md 1.16
            }
            patternsWith += any ? 1 : 0;
            CHECK(generateCandidate(style, request, seed) == p);
        }
    }
    CHECK(patternsWith > 30);
}

TEST_CASE("setChance and setCondition change the notes within their limits", "[chance][edit]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 1, 60, 0);
    add(p, 1, 62, 4);
    const std::vector<uint32_t> ids{p.voices[1].notes[0].id, p.voices[1].notes[1].id};
    CHECK(setChance(p, 1, ids, 60) == 2);
    CHECK(p.voices[1].notes[0].chance == 60);
    CHECK(setChance(p, 1, ids, 60) == 0);
    CHECK(setChance(p, 1, ids, 250) == 2);
    CHECK(p.voices[1].notes[1].chance == 100);
    CHECK(setChance(p, 1, ids, -4) == 2);
    CHECK(p.voices[1].notes[1].chance == 0);
    CHECK(setCondition(p, 1, ids, 2, 4) == 2);
    CHECK(p.voices[1].notes[0].condA == 2);
    CHECK(p.voices[1].notes[0].condB == 4);
    CHECK(setCondition(p, 1, ids, 2, 4) == 0);
    CHECK(setCondition(p, 1, ids, 9, 4) == 2); // A above B becomes B
    CHECK(p.voices[1].notes[0].condA == 4);
    CHECK(setCondition(p, 1, ids, 0, 30) == 2); // B limited to 8, A to at least 1
    CHECK(p.voices[1].notes[0].condB == 8);
    CHECK(p.voices[1].notes[0].condA == 1);
    CHECK(validatePattern(p).empty());
    CHECK(setChance(p, 7, ids, 50) == 0);
    CHECK(setCondition(p, 7, ids, 1, 2) == 0);
}

TEST_CASE("a locked rhythm keeps chance and condition as they are", "[chance][edit]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 1, 60, 0);
    add(p, 1, 62, 4);
    p.voices[1].notes[0].lock.rhythm = true;
    const std::vector<uint32_t> ids{p.voices[1].notes[0].id, p.voices[1].notes[1].id};
    CHECK(setChance(p, 1, ids, 30) == 1);
    CHECK(p.voices[1].notes[0].chance == 100);
    CHECK(p.voices[1].notes[1].chance == 30);
    CHECK(setCondition(p, 1, ids, 1, 3) == 1);
    CHECK(p.voices[1].notes[0].condB == 1);
    p.voices[1].lock.rhythm = true;
    CHECK(setChance(p, 1, ids, 10) == 0);
    CHECK(setCondition(p, 1, ids, 1, 5) == 0);
}
