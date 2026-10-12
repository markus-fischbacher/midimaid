#include "PatternFixtures.h"
#include "core/OutputStage.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/Ratchet.h"
#include "core/Turnaround.h"

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

Note& add(Pattern& p, size_t voice, int pitch, uint32_t start, uint32_t length, int velocity = 100) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    note.velocity = static_cast<uint8_t>(velocity);
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

/// 4 bars of a melody line of 16ths (240 ticks long, 180 ticks gate) with the archetype `acid_siren`.
Pattern sirenLine(uint32_t bars = 4) {
    Pattern p = makeEmptyPattern(bars, "hard_industrial");
    p.voices[1].archetypeId = "acid_siren";
    for (uint32_t step = 0; step < bars * 16; ++step) {
        add(p, 1, 60 + static_cast<int>(step % 5), step * 240, 180);
    }
    return p;
}

int ratchetsInBar(const Track& track, uint32_t bar) {
    int count = 0;
    for (const Note& note : track.notes) {
        count += note.startTick / kTicksPerBar == bar && note.ratchet > 1 ? 1 : 0;
    }
    return count;
}

OutputSettings noKick() {
    OutputSettings settings;
    settings.ignoresKick = {true, true, true};
    return settings;
}

} // namespace

TEST_CASE("ratchets only come with acid_siren and roll16_aggressive, hard styles use more", "[ratchet]") {
    CHECK(ratchetChancePct("hypnotic_motif", "hard_industrial", 100) == 0);
    CHECK(ratchetChancePct("", "peak_time", 50) == 0);
    CHECK(ratchetChancePct("acid_siren", "peak_time", 0) == 20);
    CHECK(ratchetChancePct("acid_siren", "peak_time", 100) == 40);
    CHECK(ratchetChancePct("roll16_aggressive", "melodic_techno", 50) == 30);
    CHECK(ratchetChancePct("roll16_aggressive", "hard_industrial", 0) == 40);
    CHECK(ratchetChancePct("roll16_aggressive", "hard_industrial", 100) == 70);
    CHECK(ratchetChancePct("acid_siren", "hard_industrial", 500) == 70);
}

TEST_CASE("a ratchet needs room for its repeats", "[ratchet]") {
    CHECK(maxRatchetFor(240) == 4);
    CHECK(maxRatchetFor(180) == 3);
    CHECK(maxRatchetFor(120) == 2);
    CHECK(maxRatchetFor(119) == 1);
    CHECK(maxRatchetFor(2000) == 4);
    CHECK(maxRatchetFor(100, 25) == 4);
    CHECK(maxRatchetFor(100, 0) == 4);
}

TEST_CASE("the draw sets at most one ratchet per bar, at the rate of the chance", "[ratchet]") {
    int bars = 0;
    int hits = 0;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = sirenLine();
        applyRatchets(p, 1, "hard_industrial", 100, seed, 0, 4);
        for (uint32_t bar = 0; bar < 4; ++bar) {
            const int count = ratchetsInBar(p.voices[1], bar);
            CHECK(count <= 1);
            hits += count;
            ++bars;
        }
        for (const Note& note : p.voices[1].notes) {
            CHECK(note.ratchet >= 1);
            CHECK(note.ratchet <= maxRatchetFor(note.lengthTicks));
            CHECK_FALSE((note.ratchet > 1 && note.slide));
        }
        CHECK(validatePattern(p).empty());
    }
    CHECK(hits > bars * 63 / 100);
    CHECK(hits < bars * 77 / 100);
}

TEST_CASE("only the bars of the range are drawn", "[ratchet]") {
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        Pattern p = sirenLine();
        applyRatchets(p, 1, "hard_industrial", 100, seed, 1, 3);
        CHECK(ratchetsInBar(p.voices[1], 0) == 0);
        CHECK(ratchetsInBar(p.voices[1], 3) == 0);
    }
}

TEST_CASE("the draw is a pure function of the seed and the voice is judged by its archetype", "[ratchet]") {
    Pattern a = sirenLine();
    Pattern b = sirenLine();
    applyRatchets(a, 1, "hard_industrial", 80, 42, 0, 4);
    applyRatchets(b, 1, "hard_industrial", 80, 42, 0, 4);
    CHECK(a == b);
    Pattern c = sirenLine();
    c.voices[1].archetypeId = "hypnotic_motif";
    applyRatchets(c, 1, "hard_industrial", 100, 42, 0, 4);
    for (const Note& note : c.voices[1].notes) {
        CHECK(note.ratchet == 1);
    }
    Pattern d = sirenLine();
    applyRatchets(d, 5, "hard_industrial", 100, 42, 0, 4); // unknown voice
    CHECK(d == sirenLine());
}

TEST_CASE("notes that cannot take a ratchet are left alone", "[ratchet]") {
    // all notes short, slide, chord, locked: nothing is eligible
    Pattern p = makeEmptyPattern(1, "hard_industrial");
    p.voices[1].archetypeId = "acid_siren";
    add(p, 1, 60, 0, 100);                  // too short
    add(p, 1, 62, 480, 240).slide = true;   // slide
    add(p, 1, 64, 960, 240);                // target of the slide before? no: starts a chord below
    add(p, 1, 67, 960, 240);                // chord
    Note& locked = add(p, 1, 65, 1920, 240);
    locked.lock.rhythm = true;
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Pattern q = p;
        applyRatchets(q, 1, "hard_industrial", 100, seed, 0, 1);
        for (const Note& note : q.voices[1].notes) {
            CHECK(note.ratchet == 1);
        }
    }
    // a slide target is not chosen either
    Pattern t = makeEmptyPattern(1, "hard_industrial");
    t.voices[1].archetypeId = "acid_siren";
    add(t, 1, 60, 0, 240).slide = true;
    add(t, 1, 62, 240, 240);
    for (uint64_t seed = 1; seed <= 80; ++seed) {
        Pattern q = t;
        applyRatchets(q, 1, "hard_industrial", 100, seed, 0, 1);
        CHECK(q.voices[1].notes[1].ratchet == 1);
    }
}

TEST_CASE("a voice with a locked rhythm and a bar with a ratchet are skipped", "[ratchet]") {
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern p = sirenLine();
        p.voices[1].lock.rhythm = true;
        applyRatchets(p, 1, "hard_industrial", 100, seed, 0, 4);
        CHECK(p == [&] {
            Pattern q = sirenLine();
            q.voices[1].lock.rhythm = true;
            return q;
        }());
        Pattern q = sirenLine();
        q.voices[1].notes[3].ratchet = 2;
        applyRatchets(q, 1, "hard_industrial", 100, seed, 0, 4);
        CHECK(ratchetsInBar(q.voices[1], 0) == 1);
        CHECK(q.voices[1].notes[3].ratchet == 2);
    }
}

TEST_CASE("stage 5 splits a ratchet note into equal repeats", "[ratchet][output]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 1, 60, 480, 240, 100).ratchet = 3;
    add(p, 1, 62, 1440, 240, 100);
    const OutputPattern out = renderOutput(p, noKick());
    const auto& notes = out.voices[1].notes;
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].startTick == 480);
    CHECK(notes[1].startTick == 560);
    CHECK(notes[2].startTick == 640);
    CHECK(notes[0].endTick == 480 + 60); // 3/4 of 80
    CHECK(notes[1].endTick == 560 + 60);
    CHECK(notes[2].endTick == 720);      // the last part reaches the end of the note
    CHECK(notes[0].velocity == 100);
    CHECK(notes[1].velocity == 85);
    CHECK(notes[2].velocity == 85);
    for (size_t i = 0; i < 3; ++i) {
        CHECK(notes[i].pitch == 60);
        CHECK(notes[i].noteId == notes[0].noteId);
    }
    CHECK(notes[3].startTick == 1440);
}

TEST_CASE("stage 5 keeps the accent on the first repeat and follows the groove", "[ratchet][output]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    Note& note = add(p, 1, 60, 240, 240, 90);
    note.ratchet = 2;
    note.accent = true;
    OutputSettings settings = noKick();
    const OutputPattern out = renderOutput(p, settings);
    const auto& notes = out.voices[1].notes;
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].velocity == settings.accentVelocity);
    CHECK(notes[1].velocity == settings.accentVelocity * 85 / 100);
    p.voices[1].groove.swing = 0.6f; // the 16th at 240 is an off-beat: delayed by the swing, the repeats move with it
    const OutputPattern swung = renderOutput(p, settings);
    REQUIRE(swung.voices[1].notes.size() == 2);
    CHECK(swung.voices[1].notes[0].startTick > 240);
    CHECK(swung.voices[1].notes[1].startTick - swung.voices[1].notes[0].startTick == 120);
}

TEST_CASE("stage 5 reduces repeats that would be too short and ignores slides", "[ratchet][output]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 1, 60, 0, 130).ratchet = 4;   // 130 / 4 < 60: two repeats of 65
    add(p, 1, 62, 480, 50).ratchet = 2;  // note shorter than the minimum: dropped as always
    Note& slide = add(p, 1, 64, 960, 240);
    slide.slide = true;
    slide.ratchet = 3; // invalid pattern, but the output must not ratchet a slide
    add(p, 1, 65, 1440, 240);
    const OutputPattern out = renderOutput(p, noKick());
    const auto& notes = out.voices[1].notes;
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].startTick == 0);
    CHECK(notes[1].startTick == 65);
    CHECK(notes[1].endTick == 130);
    CHECK(notes[2].startTick == 960);
    CHECK(notes[3].startTick == 1440);
}

TEST_CASE("stage 5 divides the note after the kick clearance", "[ratchet][output]") {
    Pattern p = makeEmptyPattern(1, "peak_time"); // kicks at 0, 960, ...
    add(p, 0, 36, 720, 240).ratchet = 2;           // would reach into the kick at 960
    OutputSettings settings;
    const OutputPattern out = renderOutput(p, settings);
    const auto& notes = out.voices[0].notes;
    REQUIRE(notes.size() == 2);
    CHECK(notes.back().endTick <= 960 - static_cast<int>(settings.kickClearanceTicks));
}

TEST_CASE("setRatchet sets 1 to 4 repeats and removes a slide", "[ratchet][edit]") {
    Pattern p = makeEmptyPattern(1, "peak_time");
    add(p, 1, 60, 0, 240).slide = true;
    add(p, 1, 62, 480, 240);
    const std::vector<uint32_t> ids{p.voices[1].notes[0].id, p.voices[1].notes[1].id};
    CHECK(setRatchet(p, 1, ids, 3) == 2);
    CHECK(p.voices[1].notes[0].ratchet == 3);
    CHECK_FALSE(p.voices[1].notes[0].slide);
    CHECK(p.voices[1].notes[1].ratchet == 3);
    CHECK(setRatchet(p, 1, ids, 3) == 0);
    CHECK(setRatchet(p, 1, ids, 9) == 2);
    CHECK(p.voices[1].notes[1].ratchet == 4);
    CHECK(setRatchet(p, 1, ids, 0) == 2);
    CHECK(p.voices[1].notes[0].ratchet == 1);
    CHECK(setRatchet(p, 7, ids, 2) == 0);
    CHECK(validatePattern(p).empty());
    // a slide takes the ratchet away again
    CHECK(setRatchet(p, 1, ids, 2) == 2);
    CHECK(setSlide(p, 1, ids, true) == 2);
    CHECK(p.voices[1].notes[0].ratchet == 1);
}

TEST_CASE("the generator sets ratchets only for the two archetypes, at most one per bar", "[ratchet][generator]") {
    const StyleProfile hard = loadShipped("hard_industrial");
    const StyleProfile peak = loadShipped("peak_time");
    int bassRatchets = 0;
    int sirenRatchets = 0;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        GenerationRequest request;
        request.lengthBars = 4;
        request.seed = seed;
        request.settings.energyPct = 90;
        request.bassArchetype = "roll16_aggressive";
        request.melodyArchetype = "acid_siren";
        const Pattern p = generateCandidate(hard, request, seed);
        for (uint32_t bar = 0; bar < 4; ++bar) {
            CHECK(ratchetsInBar(p.voices[0], bar) <= 1);
            CHECK(ratchetsInBar(p.voices[1], bar) <= 1);
            bassRatchets += ratchetsInBar(p.voices[0], bar);
            sirenRatchets += ratchetsInBar(p.voices[1], bar);
        }
        CHECK(validatePattern(p).empty());
        CHECK(generateCandidate(hard, request, seed) == p);
        request.bassArchetype = "hypnotic_bass_sub";
        request.melodyArchetype = "hypnotic_motif";
        const Pattern plain = generateCandidate(peak, request, seed);
        for (const Track& track : plain.voices) {
            for (const Note& note : track.notes) {
                CHECK(note.ratchet == 1);
            }
        }
    }
    CHECK(bassRatchets > 30);
    CHECK(sirenRatchets > 30);
}

TEST_CASE("regenerating a voice or a phrase also draws ratchets", "[ratchet][generator]") {
    const StyleProfile hard = loadShipped("hard_industrial");
    int voiceRatchets = 0;
    int phraseRatchets = 0;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        GenerationRequest request;
        request.lengthBars = 8;
        request.seed = seed;
        request.settings.energyPct = 100;
        request.melodyArchetype = "acid_siren";
        Phrase a;
        a.lengthBars = 4;
        Phrase b;
        b.startBar = 4;
        b.lengthBars = 4;
        b.role = PhraseRole::Variation;
        request.formPlan = std::vector<Phrase>{a, b};
        Pattern p = generateCandidate(hard, request, seed);
        REQUIRE(regenerateVoice(p, 1, hard, request, seed + 100));
        for (uint32_t bar = 0; bar < 8; ++bar) {
            CHECK(ratchetsInBar(p.voices[1], bar) <= 1);
            voiceRatchets += ratchetsInBar(p.voices[1], bar);
        }
        REQUIRE(regeneratePhrase(p, 1, hard, request, seed + 200));
        for (uint32_t bar = 4; bar < 8; ++bar) {
            CHECK(ratchetsInBar(p.voices[1], bar) <= 1);
            phraseRatchets += ratchetsInBar(p.voices[1], bar);
        }
    }
    CHECK(voiceRatchets > 15);
    CHECK(phraseRatchets > 8);
}

TEST_CASE("a ratchet turnaround repeats one late note and is the only ratchet of the bar", "[ratchet][turnaround]") {
    int seen = 0;
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        Pattern p = makeEmptyPattern(8, "hard_industrial");
        p.phrases[0].turnaround = true;
        for (uint32_t step = 0; step < 8 * 16; ++step) {
            add(p, 1, 60 + static_cast<int>(step % 4), step * 240, 240);
        }
        p.voices[1].notes[7 * 16 + 2].ratchet = 2; // an earlier ratchet in the last bar
        applyTurnaround(p, p.phrases[0], 100, seed);
        const uint32_t lastBar = 7;
        // the ratchet kind clears the earlier ratchet of the bar, every other kind leaves it where it is
        if (p.voices[1].notes[7 * 16 + 2].ratchet > 1) {
            continue;
        }
        ++seen;
        CHECK(ratchetsInBar(p.voices[1], lastBar) == 1);
        for (const Note& note : p.voices[1].notes) {
            if (note.ratchet > 1) {
                CHECK(note.startTick >= lastBar * kTicksPerBar + 2 * 960);
                CHECK(note.ratchet <= maxRatchetFor(note.lengthTicks));
                CHECK_FALSE(note.slide);
            }
        }
        CHECK(validatePattern(p).empty());
    }
    CHECK(seen > 15);
}
