#include "core/FormPlan.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/Turnaround.h"

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

constexpr uint32_t kBeat = 960;
constexpr uint32_t kLastBar = 7 * kTicksPerBar;

/// 8 bars, bass with an 8th line, melody with chords (two notes per start) in every bar.
Pattern lineAndChords() {
    Pattern p = makeEmptyPattern(8, "peak_time");
    p.phrases[0].turnaround = true;
    for (uint32_t bar = 0; bar < 8; ++bar) {
        for (uint32_t step = 0; step < 8; ++step) {
            Note note;
            note.id = allocateNoteId(p);
            note.pitch = static_cast<uint8_t>(36 + (step % 3) * 7);
            note.startTick = bar * kTicksPerBar + step * kBeat / 2;
            note.lengthTicks = kBeat / 4;
            p.voices[0].notes.push_back(note);
        }
        for (const uint8_t pitch : {60, 63}) {
            Note note;
            note.id = allocateNoteId(p);
            note.pitch = pitch;
            note.startTick = bar * kTicksPerBar + 2 * kBeat;
            note.lengthTicks = kBeat;
            p.voices[1].notes.push_back(note);
        }
    }
    return p;
}

std::vector<Note> withoutIds(std::vector<Note> notes) {
    for (Note& note : notes) {
        note.id = 0;
    }
    return notes;
}

std::vector<Note> inRange(const Track& track, uint32_t from, uint32_t to) {
    std::vector<Note> notes;
    for (const Note& n : track.notes) {
        if (n.startTick >= from && n.startTick < to) {
            notes.push_back(n);
        }
    }
    return notes;
}

} // namespace

TEST_CASE("the turnaround chance rises with the energy", "[turnaround]") {
    CHECK(turnaroundChancePct(0) == 10);
    CHECK(turnaroundChancePct(50) == 40);
    CHECK(turnaroundChancePct(100) == 70);
    CHECK(turnaroundChancePct(-20) == 10);
    CHECK(turnaroundChancePct(300) == 70);
}

TEST_CASE("only phrases of 8 and 16 bars are drawn, at the rate of the energy", "[turnaround]") {
    int lowHits = 0;
    int highHits = 0;
    const int runs = 4000;
    for (int i = 0; i < runs; ++i) {
        std::vector<Phrase> four(2);
        four[0].lengthBars = 4;
        four[1].lengthBars = 4;
        assignTurnarounds(four, 100, static_cast<uint64_t>(i));
        CHECK_FALSE(four[0].turnaround);
        CHECK_FALSE(four[1].turnaround);
        std::vector<Phrase> low(1);
        low[0].lengthBars = 8;
        assignTurnarounds(low, 0, static_cast<uint64_t>(i));
        lowHits += low[0].turnaround ? 1 : 0;
        std::vector<Phrase> high(1);
        high[0].lengthBars = 16;
        assignTurnarounds(high, 100, static_cast<uint64_t>(i));
        highHits += high[0].turnaround ? 1 : 0;
    }
    CHECK(lowHits > runs * 7 / 100);
    CHECK(lowHits < runs * 13 / 100);
    CHECK(highHits > runs * 66 / 100);
    CHECK(highHits < runs * 74 / 100);
}

TEST_CASE("the draw is a pure function of the seed and keeps flags that are set", "[turnaround]") {
    std::vector<Phrase> a(2);
    a[0].lengthBars = 8;
    a[1].lengthBars = 8;
    a[1].startBar = 8;
    auto b = a;
    assignTurnarounds(a, 60, 77);
    assignTurnarounds(b, 60, 77);
    CHECK(a == b);
    std::vector<Phrase> set(1);
    set[0].lengthBars = 8;
    set[0].turnaround = true;
    assignTurnarounds(set, 0, 5);
    CHECK(set[0].turnaround);
}

TEST_CASE("a turnaround touches only the last bar of the phrase", "[turnaround]") {
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        const Pattern before = lineAndChords();
        Pattern after = before;
        applyTurnaround(after, after.phrases[0], 50, seed);
        for (size_t v = 0; v < 2; ++v) {
            CHECK(inRange(after.voices[v], 0, kLastBar).size() == inRange(before.voices[v], 0, kLastBar).size());
            const auto a = inRange(after.voices[v], 0, kLastBar);
            const auto b = inRange(before.voices[v], 0, kLastBar);
            for (size_t i = 0; i < a.size(); ++i) {
                CHECK(a[i] == b[i]);
            }
        }
    }
}

TEST_CASE("the kinds change the last bar as described", "[turnaround]") {
    std::set<TurnaroundKind> seen;
    for (uint64_t seed = 1; seed <= 200 && seen.size() < 3; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(deriveSeed(seed, 0x7475726eULL));
        seen.insert(chooseTurnaroundKind(false, 50, rng));
    }
    CHECK(seen.size() == 3);

    // pause: nothing starts in the last two beats
    Pattern pause = lineAndChords();
    pause.voices[1].notes.clear();
    bool sawPause = false;
    bool sawFill = false;
    bool sawJump = false;
    for (uint64_t seed = 1; seed <= 200; ++seed) {
        Pattern p = lineAndChords();
        applyTurnaround(p, p.phrases[0], 50, seed);
        const auto bass = inRange(p.voices[0], kLastBar, kLastBar + kTicksPerBar);
        const auto late = inRange(p.voices[0], kLastBar + 2 * kBeat, kLastBar + kTicksPerBar);
        const auto lastBeat = inRange(p.voices[0], kLastBar + 3 * kBeat, kLastBar + kTicksPerBar);
        if (late.empty()) {
            sawPause = true;
            CHECK(bass.size() == 4);
            for (const Note& n : bass) {
                CHECK(n.startTick + n.lengthTicks <= kLastBar + 2 * kBeat);
            }
        } else if (lastBeat.size() == 4 && lastBeat[1].startTick - lastBeat[0].startTick == kBeat / 4) {
            sawFill = true;
            CHECK(lastBeat[0].startTick == kLastBar + 3 * kBeat);
            CHECK(lastBeat[3].velocity > lastBeat[0].velocity);
            CHECK(lastBeat[3].accent);
        } else {
            const Pattern original = lineAndChords();
            const auto ref = inRange(original.voices[0], kLastBar + 2 * kBeat, kLastBar + kTicksPerBar);
            if (late.size() == ref.size() && late[0].pitch != ref[0].pitch) {
                sawJump = true;
                for (size_t i = 0; i < late.size(); ++i) {
                    CHECK(std::abs(int{late[i].pitch} - int{ref[i].pitch}) == 12);
                    CHECK(late[i].startTick == ref[i].startTick);
                }
            }
        }
    }
    CHECK(sawPause);
    CHECK(sawFill);
    CHECK(sawJump);
}

TEST_CASE("a voice with chords never gets a fill", "[turnaround]") {
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        Pattern p = lineAndChords();
        applyTurnaround(p, p.phrases[0], 100, seed);
        // the chord voice only keeps or loses notes or moves them by an octave: never a run of 16ths
        for (const Note& n : inRange(p.voices[1], kLastBar, kLastBar + kTicksPerBar)) {
            CHECK(n.startTick == kLastBar + 2 * kBeat);
        }
    }
}

TEST_CASE("locked and excluded voices stay as they are", "[turnaround]") {
    const Pattern original = lineAndChords();
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pattern p = original;
        p.voices[0].lock.rhythm = true;
        applyTurnaround(p, p.phrases[0], 50, seed);
        CHECK(p.voices[0].notes == original.voices[0].notes);
        Pattern q = original;
        q.voices[1].lock.pitch = true;
        applyTurnaround(q, q.phrases[0], 50, seed);
        CHECK(q.voices[1].notes == original.voices[1].notes);
        Pattern r = original;
        applyTurnaround(r, r.phrases[0], 50, seed, {false, true});
        CHECK(r.voices[0].notes == original.voices[0].notes);
    }
}

TEST_CASE("no flag or a short phrase means no change", "[turnaround]") {
    Pattern p = lineAndChords();
    p.phrases[0].turnaround = false;
    applyTurnaround(p, p.phrases[0], 100, 3);
    CHECK(p.voices == lineAndChords().voices);
    Pattern q = lineAndChords();
    Phrase shortPhrase = q.phrases[0];
    shortPhrase.lengthBars = 4;
    applyTurnaround(q, shortPhrase, 100, 3);
    CHECK(q.voices == lineAndChords().voices);
}

TEST_CASE("the generator applies a requested turnaround to the last bar only", "[turnaround][generator]") {
    for (const std::string& name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        int differing = 0;
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            GenerationRequest request;
            request.lengthBars = 8;
            request.seed = seed;
            request.settings.energyPct = 80;
            Phrase phrase;
            phrase.lengthBars = 8;
            request.formPlan = std::vector<Phrase>{phrase};
            const Pattern plain = generateCandidate(style, request, seed);
            phrase.turnaround = true;
            request.formPlan = std::vector<Phrase>{phrase};
            const Pattern turned = generateCandidate(style, request, seed);
            INFO(name << " seed " << seed);
            CHECK(turned.phrases[0].turnaround);
            CHECK(validatePattern(turned).empty());
            bool lastBarDiffers = false;
            for (size_t v = 0; v < plain.voices.size(); ++v) {
                CHECK(inRange(turned.voices[v], 0, kLastBar).size() == inRange(plain.voices[v], 0, kLastBar).size());
                const auto a = inRange(turned.voices[v], kLastBar, 8 * kTicksPerBar);
                const auto b = inRange(plain.voices[v], kLastBar, 8 * kTicksPerBar);
                lastBarDiffers = lastBarDiffers || a.size() != b.size();
                for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
                    lastBarDiffers = lastBarDiffers || a[i].pitch != b[i].pitch || a[i].startTick != b[i].startTick;
                }
            }
            differing += lastBarDiffers ? 1 : 0;
        }
        INFO(name);
        CHECK(differing >= 6);
    }
}

TEST_CASE("a drawn turnaround shows up in generated patterns and replays exactly", "[turnaround][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    int flagged = 0;
    int plain = 0;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        GenerationRequest request;
        request.lengthBars = 8;
        request.seed = seed;
        request.settings.energyPct = 90;
        const Pattern a = generateCandidate(style, request, seed);
        const Pattern b = generateCandidate(style, request, seed);
        CHECK(a == b);
        for (const Phrase& phrase : a.phrases) {
            (phrase.turnaround ? flagged : plain)++;
        }
        for (const Phrase& phrase : a.phrases) {
            if (phrase.lengthBars < 8) {
                CHECK_FALSE(phrase.turnaround);
            }
        }
    }
    CHECK(flagged > 0);
    CHECK(plain > 0);
}

TEST_CASE("a phrase regeneration keeps its turnaround", "[turnaround][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request;
    request.lengthBars = 8;
    request.seed = 4;
    request.settings.energyPct = 80;
    Phrase a;
    a.lengthBars = 4;
    a.role = PhraseRole::Main;
    Phrase b;
    b.startBar = 4;
    b.lengthBars = 4;
    b.role = PhraseRole::Variation;
    request.formPlan = std::vector<Phrase>{a, b};
    Pattern pattern = generateCandidate(style, request, 4);
    CHECK_FALSE(pattern.phrases[1].turnaround); // 4-bar phrases get none
    REQUIRE(regeneratePhrase(pattern, 1, style, request, 9));
    CHECK_FALSE(pattern.phrases[1].turnaround);
}

TEST_CASE("a turnaround in the first of two phrases changes only the last bar of that phrase", "[turnaround][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    int differing = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        GenerationRequest request;
        request.lengthBars = 16;
        request.seed = seed;
        request.settings.energyPct = 80;
        Phrase a;
        a.lengthBars = 8;
        a.role = PhraseRole::Main;
        Phrase b;
        b.startBar = 8;
        b.lengthBars = 8;
        b.role = PhraseRole::Variation;
        request.formPlan = std::vector<Phrase>{a, b};
        const Pattern plain = generateCandidate(style, request, seed);
        a.turnaround = true;
        request.formPlan = std::vector<Phrase>{a, b};
        const Pattern turned = generateCandidate(style, request, seed);
        bool changed = false;
        for (size_t v = 0; v < plain.voices.size(); ++v) {
            CHECK(withoutIds(inRange(turned.voices[v], 0, kLastBar)) == withoutIds(inRange(plain.voices[v], 0, kLastBar)));
            // the constraint layer works across the phrase border (ties, intervals), so one note may differ
            const auto second = static_cast<int>(inRange(turned.voices[v], 8 * kTicksPerBar, 16 * kTicksPerBar).size());
            const auto secondPlain = static_cast<int>(inRange(plain.voices[v], 8 * kTicksPerBar, 16 * kTicksPerBar).size());
            CHECK(std::abs(second - secondPlain) <= 1);
            changed = changed || withoutIds(inRange(turned.voices[v], kLastBar, 8 * kTicksPerBar)) !=
                                     withoutIds(inRange(plain.voices[v], kLastBar, 8 * kTicksPerBar));
        }
        differing += changed ? 1 : 0;
    }
    CHECK(differing >= 6);
}

TEST_CASE("generating around a locked voice leaves it alone and turns the others", "[turnaround][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request;
    request.lengthBars = 8;
    request.seed = 3;
    request.settings.energyPct = 80;
    Phrase phrase;
    phrase.lengthBars = 8;
    phrase.turnaround = true;
    request.formPlan = std::vector<Phrase>{phrase};
    Pattern current = generateCandidate(style, request, 3);
    current.voices[0].lock = {true, true, true};
    const auto result = generatePatternAroundLocks(style, request, current);
    REQUIRE(result.success);
    CHECK(result.pattern.voices[0].notes == current.voices[0].notes);
    CHECK(result.pattern.phrases[0].turnaround);
}
