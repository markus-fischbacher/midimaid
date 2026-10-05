#include "PatternFixtures.h"
#include "core/Quality.h"
#include "core/Random.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <map>
#include <set>
#include <vector>

using namespace mm::core;
using namespace mm::fixtures;

namespace {

constexpr uint32_t kStep = 240;

Note& add(Pattern& p, size_t voice, uint32_t start, uint32_t length, int pitch) {
    Note note;
    note.id = allocateNoteId(p);
    note.pitch = static_cast<uint8_t>(pitch);
    note.startTick = start;
    note.lengthTicks = length;
    p.voices[voice].notes.push_back(note);
    return p.voices[voice].notes.back();
}

/// A bass bar: notes on the given steps of bar `bar`.
void bassBar(Pattern& p, uint32_t bar, std::vector<uint32_t> steps, int pitch = 33, uint32_t length = kStep) {
    for (const uint32_t step : steps) {
        add(p, 0, bar * kTicksPerBar + step * kStep, length, pitch);
    }
}

QualityContext context() {
    return QualityContext{};
}

SoftScores score(const Pattern& p, const QualityContext& c = QualityContext{}) {
    return scoreCriteria(p, c);
}

} // namespace

// -- motif ---------------------------------------------------------------------------------------------------------

TEST_CASE("motif score: share of bars with a twin, A A A A' is the full score", "[quality]") {
    auto fourBars = [](std::vector<std::vector<uint32_t>> bars) {
        Pattern p = makeEmptyPattern(4, "x");
        for (uint32_t i = 0; i < bars.size(); ++i) {
            bassBar(p, i, bars[i]);
        }
        return p;
    };
    const std::vector<uint32_t> a{2, 6, 10, 14};
    CHECK(score(fourBars({a, a, a, a})).motif == 100);
    CHECK(score(fourBars({a, a, a, {2, 6, 10}})).motif == 100); // 3 of 4 bars have a twin: 75 %
    CHECK(score(fourBars({a, a, {2, 6}, {2}})).motif == 66);    // 2 of 4: 400 * 2 / 12
    CHECK(score(fourBars({a, {2, 6, 10}, {2, 6}, {2}})).motif == 0);
    Pattern three = fourBars({a, {2, 6}, {2, 6}});
    three.lengthBars = 3;
    CHECK(score(three).motif == 88); // 2 of 3: 800 / 9
}

TEST_CASE("motif score: bars compare start, length and pitch; empty bars do not count", "[quality]") {
    Pattern p = makeEmptyPattern(2, "x");
    bassBar(p, 0, {2, 6});
    bassBar(p, 1, {2, 6}, 35); // other pitch
    CHECK(score(p).motif == 0);
    Pattern q = makeEmptyPattern(2, "x");
    bassBar(q, 0, {2, 6});
    bassBar(q, 1, {2, 6}, 33, 120); // other length
    CHECK(score(q).motif == 0);
    Pattern r = makeEmptyPattern(2, "x");
    bassBar(r, 0, {2, 6});
    bassBar(r, 1, {2, 7}); // other start
    CHECK(score(r).motif == 0);
    Pattern same = makeEmptyPattern(2, "x");
    bassBar(same, 0, {2, 6});
    bassBar(same, 1, {2, 6});
    CHECK(score(same).motif == 100);

    Pattern gap = makeEmptyPattern(4, "x");
    bassBar(gap, 0, {2, 6});
    bassBar(gap, 2, {2, 6}); // bars 1 and 3 are empty
    CHECK(score(gap).motif == 100);
    Pattern single = makeEmptyPattern(4, "x");
    bassBar(single, 1, {2, 6}); // one filled bar: nothing to compare
    CHECK(score(single).motif == 100);
    CHECK(score(makeEmptyPattern(4, "x")).motif == 100); // no notes at all
}

TEST_CASE("motif score: voices are averaged and rounded half up", "[quality]") {
    Pattern p = makeEmptyPattern(4, "x");
    const std::vector<uint32_t> a{2, 6};
    bassBar(p, 0, a);
    bassBar(p, 1, a);
    bassBar(p, 2, {3});
    bassBar(p, 3, {4}); // bass: 2 of 4 bars -> 66
    for (uint32_t bar = 0; bar < 4; ++bar) {
        add(p, 1, bar * kTicksPerBar, kStep, 72); // melody: identical bars -> 100
    }
    CHECK(score(p).motif == 83); // (66 + 100 + 1) / 2
}

// -- range ---------------------------------------------------------------------------------------------------------

TEST_CASE("range score: spans beyond the limit cost 5 points per semitone", "[quality]") {
    auto bassSpan = [](int span) {
        Pattern p = makeEmptyPattern(1, "x");
        add(p, 0, 0, kStep, 30);
        add(p, 0, kStep, kStep, 30 + span);
        return score(p).range;
    };
    CHECK(bassSpan(0) == 100);
    CHECK(bassSpan(12) == 100);
    CHECK(bassSpan(13) == 95);
    CHECK(bassSpan(20) == 60);
    CHECK(bassSpan(32) == 0);
    CHECK(bassSpan(40) == 0); // never below 0

    auto melodySpan = [](int span) {
        Pattern p = makeEmptyPattern(1, "x");
        add(p, 1, 0, kStep, 60);
        add(p, 1, kStep, kStep, 60 + span);
        return score(p).range;
    };
    CHECK(melodySpan(19) == 100);
    CHECK(melodySpan(20) == 95);
    CHECK(melodySpan(0) == 100); // two notes may share a pitch
}

TEST_CASE("range score: a melody of three notes needs a span of three semitones", "[quality]") {
    auto melody = [](int top) {
        Pattern p = makeEmptyPattern(1, "x");
        add(p, 1, 0, kStep, 60);
        add(p, 1, kStep, kStep, 60 + top);
        add(p, 1, 2 * kStep, kStep, 60);
        return score(p).range;
    };
    CHECK(melody(0) == 0);
    CHECK(melody(2) == 0);
    CHECK(melody(3) == 100);
    // a bass line on one pitch is fine
    Pattern bass = makeEmptyPattern(1, "x");
    bassBar(bass, 0, {2, 6, 10});
    CHECK(score(bass).range == 100);
}

// -- density -------------------------------------------------------------------------------------------------------

TEST_CASE("density band follows the energy", "[quality]") {
    CHECK(densityBand(VoiceRole::Bass, 0).low == 400);
    CHECK(densityBand(VoiceRole::Bass, 0).high == 800);
    CHECK(densityBand(VoiceRole::Bass, 50).low == 800);
    CHECK(densityBand(VoiceRole::Bass, 50).high == 1200);
    CHECK(densityBand(VoiceRole::Bass, 100).low == 1200);
    CHECK(densityBand(VoiceRole::Bass, 100).high == 1600);
    CHECK(densityBand(VoiceRole::Melody, 0).low == 100);
    CHECK(densityBand(VoiceRole::Melody, 0).high == 300);
    CHECK(densityBand(VoiceRole::Melody, 50).low == 250);
    CHECK(densityBand(VoiceRole::Melody, 50).high == 550);
    CHECK(densityBand(VoiceRole::Melody, 100).low == 400);
    CHECK(densityBand(VoiceRole::Melody, 100).high == 800);
    CHECK(densityBand(VoiceRole::Bass, 500).high == 1600); // energy is limited to 0-100
    CHECK(densityBand(VoiceRole::Bass, -5).low == 400);
}

TEST_CASE("density score: inside the band 100, 20 points per note outside (hand computed)", "[quality]") {
    auto bassNotes = [](int count, int energy) {
        Pattern p = makeEmptyPattern(1, "x");
        for (int i = 0; i < count; ++i) {
            add(p, 0, static_cast<uint32_t>(i) * kStep, kStep, 33);
        }
        QualityContext c;
        c.energyPct = energy;
        return score(p, c).density;
    };
    CHECK(bassNotes(8, 50) == 100);
    CHECK(bassNotes(12, 50) == 100);
    CHECK(bassNotes(7, 50) == 80);
    CHECK(bassNotes(13, 50) == 80);
    CHECK(bassNotes(2, 50) == 0);   // 600 away: 100 - 120
    CHECK(bassNotes(16, 50) == 20); // 400 away
    CHECK(bassNotes(4, 0) == 100);
    CHECK(bassNotes(8, 0) == 100);
    CHECK(bassNotes(9, 0) == 80);
    CHECK(bassNotes(16, 100) == 100);
    CHECK(bassNotes(12, 100) == 100);
    CHECK(bassNotes(8, 100) == 20);
}

TEST_CASE("density score: melody band, chords count once, chance counts less", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 1, 0, kStep, 60);
    CHECK(score(p).density == 70); // 100 at energy 50: band 250-550, deviation 150
    add(p, 1, kStep, kStep, 60);
    add(p, 1, 2 * kStep, kStep, 60);
    CHECK(score(p).density == 100); // 300
    // a chord is one onset
    Pattern chord = makeEmptyPattern(1, "x");
    for (int pitch : {60, 63, 67}) {
        add(chord, 1, 0, kStep, pitch);
    }
    add(chord, 1, kStep, kStep, 60);
    add(chord, 1, 2 * kStep, kStep, 60);
    CHECK(score(chord).density == 100); // 3 onsets, not 5

    // eight bass notes with 50 % chance are 4 expected notes: 400 away from the band 800-1200
    Pattern chance = makeEmptyPattern(1, "x");
    for (uint32_t i = 0; i < 8; ++i) {
        add(chance, 0, i * kStep, kStep, 33).chance = 50;
    }
    CHECK(score(chance).density == 20);
    // a chance above 100 counts as 100
    Pattern over = makeEmptyPattern(1, "x");
    for (uint32_t i = 0; i < 8; ++i) {
        add(over, 0, i * kStep, kStep, 33).chance = 200;
    }
    CHECK(score(over).density == 100);
}

TEST_CASE("density score: per bar over the whole pattern", "[quality]") {
    Pattern p = makeEmptyPattern(2, "x");
    for (uint32_t i = 0; i < 15; ++i) {
        add(p, 0, i * kStep, kStep, 33);
    }
    CHECK(score(p).density == 90); // 1500 / 2 = 750, 50 below 800
}

// -- kick ----------------------------------------------------------------------------------------------------------

TEST_CASE("kick score: bass notes on kick steps (4otf: 0, 4, 8, 12)", "[quality]") {
    auto bass = [](std::vector<uint32_t> steps) {
        Pattern p = makeEmptyPattern(1, "x");
        bassBar(p, 0, steps);
        return p;
    };
    CHECK(score(bass({2, 6, 10, 14})).kick == 100);
    CHECK(score(bass({0, 2, 4, 6})).kick == 50);
    CHECK(score(bass({0, 4, 8, 12})).kick == 0);
    CHECK(score(bass({0, 1, 2})).kick == 66);
    CHECK(score(makeEmptyPattern(1, "x")).kick == 100);

    // the melody never counts
    Pattern melody = bass({2, 6});
    add(melody, 1, 0, kStep, 72);
    CHECK(score(melody).kick == 100);

    // archetypes that may hold over kicks are not rated
    QualityContext ignore;
    ignore.ignoresKick = {true, false};
    CHECK(score(bass({0, 4, 8, 12}), ignore).kick == 100);
    QualityContext other;
    other.ignoresKick = {false, true}; // flag of the melody voice
    CHECK(score(bass({0, 4, 8, 12}), other).kick == 0);
}

TEST_CASE("kick score: uses the kick grid of the pattern", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    p.kickGridId = "halftime"; // kick on steps 0 and 8
    bassBar(p, 0, {4, 12});
    CHECK(score(p).kick == 100);
    Pattern q = makeEmptyPattern(1, "x");
    q.kickGridId = "halftime";
    bassBar(q, 0, {0, 8});
    CHECK(score(q).kick == 0);
}

// -- intervals -----------------------------------------------------------------------------------------------------

TEST_CASE("interval score: tense intervals on strong steps (hand computed)", "[quality]") {
    // bass A1 (33) holds one beat; the melody sounds with it
    auto pair = [](int melodyPitch) {
        Pattern p = makeEmptyPattern(1, "x");
        add(p, 0, 0, 960, 33);
        add(p, 1, 0, 960, melodyPitch);
        return p;
    };
    CHECK(score(pair(72)).intervals == 100); // C5: a minor third above, 39 semitones
    CHECK(score(pair(70)).intervals == 0);   // 37 semitones: a minor second
    CHECK(score(pair(75)).intervals == 0);   // 42: tritone
    CHECK(score(pair(76)).intervals == 100); // 43: perfect fifth
    CHECK(score(pair(80)).intervals == 0);   // 47: major seventh

    QualityContext tense;
    tense.chromaticPercent = 30;
    CHECK(score(pair(70), tense).intervals == 100);
    QualityContext almost;
    almost.chromaticPercent = 29;
    CHECK(score(pair(70), almost).intervals == 0);
    QualityContext harsh;
    harsh.harshStyle = true;
    CHECK(score(pair(70), harsh).intervals == 100);
}

TEST_CASE("interval score: register rule at a simultaneous attack applies even when tension is allowed", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 0, 960, 33);
    add(p, 1, 0, 960, 40); // less than 12 above the bass
    QualityContext harsh;
    harsh.harshStyle = true;
    CHECK(score(p, harsh).intervals == 0);
    CHECK(score(p).intervals == 0);
}

TEST_CASE("interval score: only notes that sound with the bass count", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 0, 480, 33);   // the bass ends before the next beat
    add(p, 1, 0, 480, 72);   // sounds with the bass: counts, clean
    add(p, 1, 480, 240, 70); // starts after the bass ended, no strong step inside: not rated
    CHECK(score(p).intervals == 100);
    add(p, 1, 960, 480, 69); // bass is silent on the strong step: not rated
    CHECK(score(p).intervals == 100);

    Pattern mixed = makeEmptyPattern(1, "x");
    add(mixed, 0, 0, 1920, 33);
    add(mixed, 1, 0, 960, 72);   // clean
    add(mixed, 1, 960, 960, 70); // minor second against the sounding bass on beat 2
    CHECK(score(mixed).intervals == 50);

    Pattern none = makeEmptyPattern(1, "x");
    add(none, 1, 0, 960, 70); // no bass at all
    CHECK(score(none).intervals == 100);
}

TEST_CASE("interval score: a held melody note meets the bass on a later strong step", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    add(p, 0, 960, 960, 33); // bass starts on beat 2
    add(p, 1, 480, 960, 70); // melody starts earlier, holds into beat 2: minor second there
    CHECK(score(p).intervals == 0);
}

// -- variety -------------------------------------------------------------------------------------------------------

TEST_CASE("variety score: different note lengths", "[quality]") {
    auto lengths = [](std::vector<uint32_t> values) {
        Pattern p = makeEmptyPattern(1, "x");
        uint32_t start = 0;
        for (const uint32_t length : values) {
            add(p, 0, start, length, 33);
            start += 480;
        }
        return score(p).rhythmVariety;
    };
    CHECK(lengths({240}) == 0);
    CHECK(lengths({240, 240, 240}) == 0);
    CHECK(lengths({240, 120}) == 50);
    CHECK(lengths({240, 120, 180}) == 100);
    CHECK(lengths({240, 120, 180, 60}) == 100);
    CHECK(score(makeEmptyPattern(1, "x")).rhythmVariety == 100);
}

// -- overall -------------------------------------------------------------------------------------------------------

TEST_CASE("overallScore: weighted mean (hand computed)", "[quality]") {
    SoftScores s;
    s.motif = 80;
    s.range = 60;
    s.density = 40;
    s.kick = 100;
    s.intervals = 20;
    s.rhythmVariety = 0;
    const QualityProfile w;               // 3 1 2 2 2 1
    CHECK(overallScore(s, w, 0) == 56);   // 620 / 11
    CHECK(overallScore(s, w, 50) == 55);  // motif weight 3 * 0.75: 112000 / 2050
    CHECK(overallScore(s, w, 100) == 53); // motif weight 1.5: 100000 / 1900
}

TEST_CASE("overallScore: rounding, zero weights, clamping", "[quality]") {
    QualityProfile two;
    two.motif = 1;
    two.range = 1;
    two.density = two.kick = two.intervals = two.rhythmVariety = 0;
    SoftScores s;
    s.motif = 50;
    s.range = 51;
    CHECK(overallScore(s, two, 0) == 51); // 50.5 rounds up
    s.motif = 51;
    s.range = 52;
    CHECK(overallScore(s, two, 0) == 52); // 51.5
    s.motif = 50;
    s.range = 50;
    CHECK(overallScore(s, two, 0) == 50);

    QualityProfile none;
    none.motif = none.range = none.density = none.kick = none.intervals = none.rhythmVariety = 0;
    CHECK(overallScore(SoftScores{0, 0, 0, 0, 0, 0}, none, 0) == 100);

    QualityProfile negative = two;
    negative.motif = -5; // counts as 0
    s.motif = 0;
    s.range = 80;
    CHECK(overallScore(s, negative, 0) == 80);

    // creativity outside 0-100 is limited
    SoftScores t{80, 60, 40, 100, 20, 0};
    CHECK(overallScore(t, QualityProfile{}, 150) == overallScore(t, QualityProfile{}, 100));
    CHECK(overallScore(t, QualityProfile{}, -10) == overallScore(t, QualityProfile{}, 0));

    // creativity weakens the motif criterion only: a bad motif score hurts less
    SoftScores badMotif{0, 100, 100, 100, 100, 100};
    CHECK(overallScore(badMotif, QualityProfile{}, 100) > overallScore(badMotif, QualityProfile{}, 0));
    // ... and the other criteria weigh more: 1000 / 11 = 91 without, 850 / 9.5 = 89 with full creativity
    SoftScores badRange{100, 0, 100, 100, 100, 100};
    CHECK(overallScore(badRange, QualityProfile{}, 0) == 91);
    CHECK(overallScore(badRange, QualityProfile{}, 100) == 89);
}

TEST_CASE("property: scores stay in 0-100 and the overall score between the criteria", "[quality][property]") {
    Pcg32 rng(31, 5);
    for (int round = 0; round < 300; ++round) {
        const Pattern p = randomPattern(rng);
        QualityContext c;
        c.energyPct = static_cast<int>(rng.bounded(101));
        c.creativityPct = static_cast<int>(rng.bounded(101));
        c.chromaticPercent = static_cast<int>(rng.bounded(101));
        c.harshStyle = rng.chance(30);
        const SoftScores s = score(p, c);
        for (const int v : {s.motif, s.range, s.density, s.kick, s.intervals, s.rhythmVariety}) {
            CHECK(v >= 0);
            CHECK(v <= 100);
        }
        QualityProfile w;
        w.motif = static_cast<int>(rng.bounded(6));
        w.range = static_cast<int>(rng.bounded(6));
        w.density = static_cast<int>(rng.bounded(6));
        w.kick = static_cast<int>(rng.bounded(6));
        w.intervals = static_cast<int>(rng.bounded(6));
        w.rhythmVariety = static_cast<int>(rng.bounded(6));
        int low = 100;
        int high = 0;
        const std::pair<int, int> weighted[] = {{w.motif, s.motif},         {w.range, s.range},
                                                {w.density, s.density},     {w.kick, s.kick},
                                                {w.intervals, s.intervals}, {w.rhythmVariety, s.rhythmVariety}};
        bool any = false;
        for (const auto& [weight, value] : weighted) {
            if (weight > 0) {
                any = true;
                low = std::min(low, value);
                high = std::max(high, value);
            }
        }
        const int overall = overallScore(s, w, c.creativityPct);
        if (any) {
            CHECK(overall >= low);
            CHECK(overall <= high);
        } else {
            CHECK(overall == 100);
        }
    }
}

// -- selection -----------------------------------------------------------------------------------------------------

namespace {

/// A bass bar with four notes of which `hits` sit on kick steps: the kick score is (4 - hits) * 25.
Pattern kickPattern(int hits) {
    Pattern p = makeEmptyPattern(1, "x");
    const uint32_t kickSteps[] = {0, 4, 8, 12};
    const uint32_t freeSteps[] = {1, 2, 3, 5};
    for (int i = 0; i < 4; ++i) {
        const uint32_t step = i < hits ? kickSteps[i] : freeSteps[i - hits];
        add(p, 0, step * kStep, kStep, 33);
    }
    return p;
}

QualityProfile kickOnly(int minScore) {
    QualityProfile profile;
    profile.minScore = minScore;
    profile.motif = profile.range = profile.density = profile.intervals = profile.rhythmVariety = 0;
    profile.kick = 1;
    return profile;
}

/// Generator whose pattern for a candidate seed is given by `hitsBySeed`: score (4 - hits) * 25.
struct Table {
    std::map<uint64_t, int> hitsBySeed;
    int calls = 0;

    CandidateGenerator generator() {
        return [this](uint64_t seed) {
            ++calls;
            const auto it = hitsBySeed.find(seed);
            Pattern p = kickPattern(it == hitsBySeed.end() ? 4 : it->second); // unknown seeds score 0
            p.info.seed = seed;
            return p;
        };
    }

    void set(uint64_t init, int round, int index, int hits) { hitsBySeed[candidateSeed(init, round, index)] = hits; }
};

constexpr uint64_t kInit = 12345;

} // namespace

TEST_CASE("candidate seeds are derived from the initial seed and distinct", "[quality]") {
    std::set<uint64_t> seen;
    for (int round = 0; round < kMaxRounds; ++round) {
        for (int index = 0; index < kCandidatesPerRound; ++index) {
            const uint64_t seed = candidateSeed(kInit, round, index);
            CHECK(seed == deriveSeed(kInit, static_cast<uint64_t>(round * 8 + index)));
            seen.insert(seed);
        }
    }
    CHECK(seen.size() == 24);
    CHECK(candidateSeed(1, 0, 0) != candidateSeed(2, 0, 0));
    CHECK(candidateSeed(kInit, 0, 0) == candidateSeed(kInit, 0, 0));
}

TEST_CASE("selection: the best valid candidate of the first round wins", "[quality]") {
    Table table;
    const int hits[] = {3, 2, 1, 0, 1, 2, 4, 3}; // scores 25 50 75 100 75 50 0 25
    for (int i = 0; i < 8; ++i) {
        table.set(kInit, 0, i, hits[i]);
    }
    const SelectionResult result = selectBest(table.generator(), kInit, kickOnly(50), context());
    REQUIRE(result.success);
    CHECK(result.score == 100);
    CHECK(result.seed == candidateSeed(kInit, 0, 3));
    CHECK(result.rounds == 1);
    CHECK(result.candidates == 8);
    CHECK(table.calls == 8);
    CHECK(result.pattern.info.seed == result.seed);
    CHECK(result.scores.kick == 100);
}

TEST_CASE("selection: a tie goes to the lower index", "[quality]") {
    Table table;
    const int hits[] = {4, 4, 1, 4, 4, 1, 4, 4}; // 75 at index 2 and 5
    for (int i = 0; i < 8; ++i) {
        table.set(kInit, 0, i, hits[i]);
    }
    const SelectionResult result = selectBest(table.generator(), kInit, kickOnly(50), context());
    REQUIRE(result.success);
    CHECK(result.seed == candidateSeed(kInit, 0, 2));
    CHECK(result.score == 75);
}

TEST_CASE("selection: the minimum score is inclusive", "[quality]") {
    Table table;
    for (int i = 0; i < 8; ++i) {
        table.set(kInit, 0, i, i == 5 ? 1 : 4); // one candidate with 75, the others 0
    }
    CHECK(selectBest(table.generator(), kInit, kickOnly(75), context()).success);
    CHECK(selectBest(table.generator(), kInit, kickOnly(75), context()).seed == candidateSeed(kInit, 0, 5));
    const SelectionResult strict = selectBest(table.generator(), kInit, kickOnly(76), context());
    CHECK_FALSE(strict.success);
}

TEST_CASE("selection: further rounds follow when nobody qualifies", "[quality]") {
    Table table;               // everything scores 0 unless set
    table.set(kInit, 1, 4, 0); // round 2 (index 1), candidate 4: score 100
    table.set(kInit, 0, 2, 3); // score 25 in round 1, below the minimum of 50
    const SelectionResult second = selectBest(table.generator(), kInit, kickOnly(50), context());
    REQUIRE(second.success);
    CHECK(second.rounds == 2);
    CHECK(second.candidates == 16);
    CHECK(second.seed == candidateSeed(kInit, 1, 4));
    CHECK(second.score == 100);

    Table third;
    third.set(kInit, 2, 7, 2); // score 50, round 3
    const SelectionResult last = selectBest(third.generator(), kInit, kickOnly(50), context());
    REQUIRE(last.success);
    CHECK(last.rounds == 3);
    CHECK(last.candidates == 24);
    CHECK(last.seed == candidateSeed(kInit, 2, 7));
}

TEST_CASE("selection: without a valid candidate after three rounds nothing is taken", "[quality]") {
    Table table;
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 8; ++i) {
            table.set(kInit, round, i, 3); // 25, below the minimum
        }
    }
    const SelectionResult result = selectBest(table.generator(), kInit, kickOnly(50), context());
    CHECK_FALSE(result.success);
    CHECK(result.rounds == 3);
    CHECK(result.candidates == 24);
    CHECK(table.calls == 24);
    CHECK(result.pattern.voices.empty()); // never an under-rated candidate
    CHECK(result.score == 0);
}

TEST_CASE("selection: a round with a valid candidate ends the search, earlier rounds are not reconsidered",
          "[quality]") {
    Table table;
    table.set(kInit, 0, 0, 3); // 25 in round 1: below the minimum
    table.set(kInit, 1, 6, 2); // 50 in round 2
    table.set(kInit, 2, 0, 0); // 100 in round 3 is never reached
    const SelectionResult result = selectBest(table.generator(), kInit, kickOnly(50), context());
    REQUIRE(result.success);
    CHECK(result.seed == candidateSeed(kInit, 1, 6));
    CHECK(result.rounds == 2);
    CHECK(table.calls == 16);
}

TEST_CASE("selection: the hard check rejects candidates", "[quality]") {
    Table table;
    const int hits[] = {4, 4, 4, 0, 1, 4, 4, 4}; // 100 at index 3, 75 at index 4
    for (int i = 0; i < 8; ++i) {
        table.set(kInit, 0, i, hits[i]);
    }
    int checks = 0;
    const uint64_t bestSeed = candidateSeed(kInit, 0, 3);
    const SelectionResult result = selectBest(table.generator(), kInit, kickOnly(50), context(), [&](const Pattern& p) {
        ++checks;
        return p.info.seed != bestSeed;
    });
    REQUIRE(result.success);
    CHECK(result.seed == candidateSeed(kInit, 0, 4));
    CHECK(result.score == 75);
    CHECK(checks == 8);

    const SelectionResult rejectAll =
        selectBest(table.generator(), kInit, kickOnly(0), context(), [](const Pattern&) { return false; });
    CHECK_FALSE(rejectAll.success);
    CHECK(rejectAll.candidates == 24);
}

TEST_CASE("selection: deterministic, and the winner seed replays the candidate", "[quality]") {
    // every candidate seed gives a different pattern; the winner is reproduced by its seed alone
    CandidateGenerator generate = [](uint64_t seed) { return kickPattern(static_cast<int>(seed % 5)); };
    const SelectionResult a = selectBest(generate, 777, kickOnly(0), context());
    const SelectionResult b = selectBest(generate, 777, kickOnly(0), context());
    REQUIRE(a.success);
    CHECK(a.seed == b.seed);
    CHECK(a.pattern == b.pattern);
    CHECK(generate(a.seed) == a.pattern);
    // another initial seed picks from other candidates
    bool different = false;
    for (uint64_t init = 0; init < 20; ++init) {
        different = different || selectBest(generate, init, kickOnly(0), context()).seed != a.seed;
    }
    CHECK(different);
}

TEST_CASE("selection: the score uses the context and the profile weights", "[quality]") {
    // with the default weights the kick criterion is only one of six
    QualityProfile profile;
    profile.minScore = 0;
    Table table;
    for (int i = 0; i < 8; ++i) {
        table.set(kInit, 0, i, i == 2 ? 0 : 4);
    }
    const SelectionResult result = selectBest(table.generator(), kInit, profile, context());
    REQUIRE(result.success);
    CHECK(result.seed == candidateSeed(kInit, 0, 2));
    CHECK(result.score == overallScore(result.scores, profile, context().creativityPct));
    CHECK(result.scores == scoreCriteria(result.pattern, context()));
}

TEST_CASE("applyResult stores score and winner seed", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    SelectionResult result;
    result.score = 77;
    result.seed = 0xDEADBEEFCAFEull;
    applyResult(p, result);
    CHECK(p.qualityScore == 77);
    CHECK(p.info.winnerSeed == 0xDEADBEEFCAFEull);
    result.score = 400; // limited to 100
    applyResult(p, result);
    CHECK(p.qualityScore == 100);
}

TEST_CASE("mean over voices rounds half up (two bass voices, kick scores 75 and 50)", "[quality]") {
    Pattern p = makeEmptyPattern(1, "x");
    const Track secondBass = p.voices[0];
    p.voices.push_back(secondBass);
    // first bass: 4 notes with one on a kick step -> 75; second bass: 2 of 4 on kick steps -> 50
    bassBar(p, 0, {0, 1, 2, 3});
    for (const uint32_t step : {0u, 4u, 5u, 6u}) {
        add(p, 2, step * kStep, kStep, 33);
    }
    p.voices[2].role = VoiceRole::Bass;
    CHECK(score(p).kick == 63); // 62.5 rounds up
}

TEST_CASE("interval score: notes that do not sound with the bass dilute nothing", "[quality]") {
    Pattern p = makeEmptyPattern(2, "x");
    add(p, 0, 0, 1920, 33);
    add(p, 1, 0, 960, 72);                  // clean
    add(p, 1, 960, 960, 70);                // minor second on beat 2
    add(p, 1, kTicksPerBar + 960, 480, 72); // second bar: no bass, not rated
    CHECK(score(p).intervals == 50);
}

TEST_CASE("overallScore: a negative weight counts as 0 for every criterion", "[quality]") {
    const SoftScores s{10, 20, 30, 40, 50, 60};
    const int values[] = {10, 20, 30, 40, 50, 60};
    for (int negative = 0; negative < 6; ++negative) {
        const int positive = (negative + 1) % 6;
        QualityProfile w;
        int* fields[] = {&w.motif, &w.range, &w.density, &w.kick, &w.intervals, &w.rhythmVariety};
        for (int* field : fields) {
            *field = 0;
        }
        *fields[negative] = -3;
        *fields[positive] = 1;
        INFO("negative weight on criterion " << negative);
        CHECK(overallScore(s, w, 0) == values[positive]);
    }
}

TEST_CASE("interval score: relevance edges (attack, end of the bass note, start of the melody note)", "[quality]") {
    // a simultaneous attack on a weak step is rated: the melody sits less than 12 above the bass
    Pattern attack = makeEmptyPattern(1, "x");
    add(attack, 0, 480, 240, 33);
    add(attack, 1, 480, 240, 40);
    CHECK(score(attack).intervals == 0);

    // the bass note ends exactly on beat 2: it does not sound there, so the melody note holding over beat 2 is not
    // rated; the second bar holds one rated violation
    Pattern ends = makeEmptyPattern(2, "x");
    add(ends, 0, 0, 960, 33);
    add(ends, 1, 480, 960, 70); // not rated
    add(ends, 0, kTicksPerBar, 960, 33);
    add(ends, 1, kTicksPerBar, 960, 70); // rated, minor second
    CHECK(score(ends).intervals == 0);

    // the bass sounds only before the melody note starts: the strong step before the note does not count
    Pattern before = makeEmptyPattern(2, "x");
    add(before, 0, 0, 480, 33);
    add(before, 1, 480, 960, 72); // not rated
    add(before, 0, kTicksPerBar, 960, 33);
    add(before, 1, kTicksPerBar, 960, 70); // rated, minor second
    CHECK(score(before).intervals == 0);
}
