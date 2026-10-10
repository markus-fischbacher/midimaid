#include "core/ChordSymbol.h"
#include "core/PatternGenerator.h"
#include "core/ReferenceProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

using namespace mm::core;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

namespace {

StyleProfile shipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

Chord chord(const char* symbol) {
    const auto parsed = parseChordSymbol(symbol);
    REQUIRE(parsed.has_value());
    return *parsed;
}

std::vector<Chord> chords(std::initializer_list<const char*> symbols) {
    std::vector<Chord> result;
    for (const char* symbol : symbols) {
        result.push_back(chord(symbol));
    }
    return result;
}

/// One chord per bar.
std::vector<ChordGuess> perBar(const std::vector<Chord>& list) {
    std::vector<ChordGuess> guesses;
    uint32_t bar = 0;
    for (const Chord& c : list) {
        ChordGuess guess;
        guess.chord = c;
        guess.startHalfBar = bar * 2;
        guess.lengthHalfBars = 2;
        guess.confidence = 0.9;
        guesses.push_back(guess);
        ++bar;
    }
    return guesses;
}

ReferenceItem item(const std::string& id, TrackRole role, const std::vector<Chord>& progression = {},
                   double confidence = 0.8, int weight = 3) {
    ReferenceItem result;
    result.id = id;
    result.name = id;
    result.role = role;
    result.weight = weight;
    result.lengthBars = 4;
    result.analysis.role = role;
    result.analysis.key.root = 9;
    result.analysis.key.scaleId = "natural_minor";
    result.analysis.progression = perBar(progression);
    result.analysis.progressionConfidence = confidence;
    result.analysis.lengthBars = 4;
    result.analysis.rhythm.bars.assign(4, std::bitset<16>(0x1111)); // four notes per bar
    for (uint32_t bar = 0; bar < 4; ++bar) {
        for (uint32_t step = 0; step < 4; ++step) {
            MidiClipNote note;
            note.pitch = static_cast<uint8_t>(role == TrackRole::Bass ? 45 + step * 3 : 69 + step * 2);
            note.startTick = bar * kTicksPerBar + step * 4 * (kTicksPerBar / 16);
            note.lengthTicks = kTicksPerBar / 16 * 2;
            result.notes.push_back(note);
        }
    }
    return result;
}

ReferenceSet setOf(std::vector<ReferenceItem> items) {
    ReferenceSet set;
    set.styleId = "peak_time";
    set.items = std::move(items);
    return set;
}

/// Five bass items with the cycle i III VII, so that the progressions take effect.
ReferenceSet fiveWith(const std::vector<Chord>& progression, double confidence = 0.8, int weight = 3) {
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        items.push_back(item("b" + std::to_string(i), TrackRole::Bass, progression, confidence, weight));
    }
    return setOf(std::move(items));
}

} // namespace

// ---- protection entries ---------------------------------------------------------------------------------------

TEST_CASE("protection entries are the active bass and melody items, with their notes", "[reference-profile]") {
    auto bass = item("a", TrackRole::Bass);
    auto melody = item("b", TrackRole::Melody);
    auto off = item("c", TrackRole::Bass);
    off.active = false;
    const auto set = setOf({bass, melody, off, item("d", TrackRole::Drums), item("e", TrackRole::Chords)});
    const auto entries = protectionEntries(set);
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].role == VoiceRole::Bass);
    CHECK(entries[1].role == VoiceRole::Melody);
    CHECK(entries[0].bars == 4);
    REQUIRE(entries[0].notes.size() == bass.notes.size());
    CHECK(entries[0].notes[1].pitch == bass.notes[1].pitch);
    CHECK(entries[0].notes[1].startTick == bass.notes[1].startTick);
    CHECK(entries[0].notes[1].lengthTicks == bass.notes[1].lengthTicks);
    CHECK(entries[0].active);
    CHECK(protectionEntries(ReferenceSet{}).empty());
}

// ---- personal progressions ------------------------------------------------------------------------------------

TEST_CASE("the progressions of the set take effect from five active items on", "[reference-profile]") {
    const auto cycle = chords({"i", "III", "VII"});
    auto set = fiveWith(cycle);
    REQUIRE(personalProgressions(set).size() == 1);
    CHECK(personalProgressions(set)[0].chords == cycle);
    set.items[0].active = false;
    CHECK(personalProgressions(set).empty()); // only four are active
    set.items[0].active = true;
    set.items.pop_back();
    CHECK(personalProgressions(set).empty()); // only four items
}

TEST_CASE("inactive items, drums, unsure and single-chord progressions give no cycle", "[reference-profile]") {
    const auto cycle = chords({"i", "VI", "VII"});
    auto set = fiveWith(cycle);
    set.items.push_back(item("off", TrackRole::Melody, chords({"i", "iv"})));
    set.items.back().active = false;
    set.items.push_back(item("drums", TrackRole::Drums, chords({"i", "v"})));
    set.items.push_back(item("unsure", TrackRole::Melody, chords({"i", "v"}), kMinProgressionConfidence - 0.05));
    set.items.push_back(item("one", TrackRole::Melody, chords({"i", "i", "i", "i"})));
    const auto result = personalProgressions(set);
    REQUIRE(result.size() == 1);
    CHECK(result[0].chords == cycle);
    // a progression at exactly the minimum confidence counts
    set.items.push_back(item("edge", TrackRole::Melody, chords({"i", "v"}), kMinProgressionConfidence));
    CHECK(personalProgressions(set).size() == 2);
}

TEST_CASE("the cycle is the shortest repetition of two to four chords", "[reference-profile]") {
    SECTION("a repeated two-chord loop") {
        const auto result = personalProgressions(fiveWith(chords({"i", "VI", "i", "VI", "i", "VI", "i", "VI"})));
        REQUIRE(result.size() == 1);
        CHECK(result[0].chords == chords({"i", "VI"}));
    }
    SECTION("equal neighbours count once") {
        const auto result = personalProgressions(fiveWith(chords({"i", "i", "VI", "VI", "VII", "VII"})));
        REQUIRE(result.size() == 1);
        CHECK(result[0].chords == chords({"i", "VI", "VII"}));
    }
    SECTION("a loop repeated over the whole clip") {
        const auto result = personalProgressions(fiveWith(chords({"i", "III", "VII", "i", "III", "VII"})));
        REQUIRE(result.size() == 1);
        CHECK(result[0].chords == chords({"i", "III", "VII"}));
    }
    SECTION("four different chords") {
        const auto result = personalProgressions(fiveWith(chords({"i", "iv", "VI", "VII"})));
        REQUIRE(result.size() == 1);
        CHECK(result[0].chords == chords({"i", "iv", "VI", "VII"}));
    }
    SECTION("a sequence that never repeats within four chords gives none") {
        CHECK(personalProgressions(fiveWith(chords({"i", "iv", "VI", "VII", "v"}))).empty());
    }
    SECTION("a change at half-bar level") {
        auto set = fiveWith(chords({"i", "VI"}));
        for (auto& it : set.items) {
            it.analysis.progression.clear();
            const auto list = chords({"i", "VI", "i", "VI"});
            uint32_t half = 0;
            for (const Chord& c : list) {
                ChordGuess guess;
                guess.chord = c;
                guess.startHalfBar = half++;
                guess.lengthHalfBars = 1;
                guess.confidence = 0.9;
                it.analysis.progression.push_back(guess);
            }
        }
        const auto result = personalProgressions(set);
        REQUIRE(result.size() == 1);
        CHECK(result[0].chords == chords({"i", "VI"}));
    }
}

TEST_CASE("the weight of a cycle is item weight times confidence, summed for equal cycles", "[reference-profile]") {
    const auto a = chords({"i", "VI"});
    const auto b = chords({"i", "VII"});
    std::vector<ReferenceItem> items;
    items.push_back(item("1", TrackRole::Bass, a, 0.8, 3)); // 3 * 80 / 10 = 24
    items.push_back(item("2", TrackRole::Melody, a, 0.5, 4)); // 4 * 50 / 10 = 20
    items.push_back(item("3", TrackRole::Bass, b, 1.0, 1));   // 1 * 100 / 10 = 10
    items.push_back(item("4", TrackRole::Bass, b, 0.3, 1));   // 1 * 30 / 10 = 3
    items.push_back(item("5", TrackRole::Bass, chords({"i", "iv"}), 0.3, 1));
    const auto result = personalProgressions(setOf(items));
    REQUIRE(result.size() == 3);
    // sorted by the chords: i iv, i VI, i VII (by root offset, then quality)
    for (const auto& p : result) {
        if (p.chords == a) {
            CHECK(p.weight == 44);
        } else if (p.chords == b) {
            CHECK(p.weight == 13);
        } else {
            CHECK(p.weight == 3);
        }
    }
    CHECK(result[0].chords[1].rootOffset <= result[1].chords[1].rootOffset);
    CHECK(result[1].chords[1].rootOffset <= result[2].chords[1].rootOffset);
}

TEST_CASE("a progression with low weight still counts at least one", "[reference-profile]") {
    const auto result = personalProgressions(fiveWith(chords({"i", "VI"}), 0.3, 1));
    REQUIRE(result.size() == 1);
    CHECK(result[0].weight == 15); // five items of 3
    // the lowest possible item: weight 1 at confidence 0.3 is 3, never 0
    std::vector<ReferenceItem> items;
    for (int i = 0; i < 5; ++i) {
        items.push_back(item("x" + std::to_string(i), TrackRole::Bass, chords({"i", "VI"}), 0.3, 1));
    }
    items[0].analysis.progressionConfidence = 0.3;
    CHECK(personalProgressions(setOf(items))[0].weight >= 1);
}

// ---- generation -----------------------------------------------------------------------------------------------

namespace {

StyleProfile fourChordStyle() {
    StyleProfile style = shipped("peak_time");
    style.harmony.modes = {{"four_chord", 1}};
    style.harmony.progressions = {chords({"i", "iv", "VI", "VII"})};
    style.harmony.chordLengthBarsMin = 1;
    style.harmony.chordLengthBarsMax = 1;
    return style;
}

double personalShare(const std::vector<WeightedProgression>& personal) {
    const auto style = fourChordStyle();
    int hits = 0;
    const int runs = 600;
    for (int seed = 1; seed <= runs; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(static_cast<uint64_t>(seed));
        const auto events = generateProgression(style, 4, rng, personal);
        if (events.size() == 4 && events[1].chord == chord("III")) {
            ++hits;
        }
    }
    return static_cast<double>(hits) / runs;
}

} // namespace

TEST_CASE("personal progressions join the draw and never take more than half of it", "[reference-profile]") {
    const std::vector<Chord> own = chords({"i", "III", "VI", "VII"});
    // a huge weight is cut to the weight of the profile: half of the draws
    const double heavy = personalShare({{own, 100000}});
    CHECK(heavy > 0.42);
    CHECK(heavy < 0.58);
    // a small weight: 1 against 20
    const double light = personalShare({{own, 1}});
    CHECK(light < 0.12);
    // equal weight to the profile's progression: half
    const double equal = personalShare({{own, kStyleProgressionWeight}});
    CHECK(equal > 0.42);
    CHECK(equal < 0.58);
    CHECK(personalShare({}) == 0.0);
}

TEST_CASE("without personal progressions the progression is the one of before", "[reference-profile]") {
    for (const auto& name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const auto style = shipped(name);
        for (uint64_t seed = 1; seed <= 40; ++seed) {
            Pcg32 a = Pcg32::fromSeed(seed);
            Pcg32 b = Pcg32::fromSeed(seed);
            CHECK(generateProgression(style, 8, a) == generateProgression(style, 8, b, {}));
        }
    }
}

TEST_CASE("a personal progression fits the mode like a style progression", "[reference-profile]") {
    StyleProfile style = fourChordStyle();
    style.harmony.progressions.clear(); // the profile has nothing: only the personal one can come
    style.harmony.modes = {{"two_chord", 1}};
    const std::vector<WeightedProgression> personal{{chords({"i", "III", "VII"}), 10}};
    Pcg32 rng = Pcg32::fromSeed(7);
    const auto events = generateProgression(style, 4, rng, personal);
    REQUIRE(events.size() >= 2);
    CHECK(events[0].chord == chord("i"));
    CHECK(events[1].chord == chord("III")); // the three chords are cut to the first two
}

TEST_CASE("a personal progression that does not fit a static mode is ignored", "[reference-profile]") {
    StyleProfile style = fourChordStyle();
    style.harmony.modes = {{"static", 1}};
    Pcg32 rng = Pcg32::fromSeed(7);
    const auto events = generateProgression(style, 4, rng, {{chords({"i", "III"}), 50}});
    REQUIRE(events.size() == 1);
    CHECK(events[0].chord == chord("i"));
    CHECK(events[0].lengthHalfBars == 8);
}

TEST_CASE("the request carries the progressions and the set id into the pattern", "[reference-profile]") {
    const auto style = fourChordStyle();
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 11;
    request.personalProgressions = {{chords({"i", "III", "VI", "VII"}), 100000}};
    request.referenceSetId = kReferenceSetId;
    const auto with = generateCandidate(style, request, 5);
    CHECK(with.info.referenceSetId == "default");
    request.referenceSetId.clear();
    CHECK(generateCandidate(style, request, 5).info.referenceSetId.empty());
    // the same candidate seed replays the same chords with the same request
    CHECK(generateCandidate(style, request, 5).context.progression == generateCandidate(style, request, 5).context.progression);
    // and a request without the progressions is the old one
    GenerationRequest plain;
    plain.lengthBars = 4;
    plain.seed = 11;
    GenerationRequest withNone = plain;
    withNone.personalProgressions.clear();
    CHECK(generateCandidate(style, plain, 5) == generateCandidate(style, withNone, 5));
}

// ---- examples -------------------------------------------------------------------------------------------------

TEST_CASE("examples: the best bass, then the best melody, then the best of the rest", "[reference-profile]") {
    auto set = setOf({item("b1", TrackRole::Bass, {}, 0.8, 2), item("b2", TrackRole::Bass, {}, 0.8, 5),
                      item("m1", TrackRole::Melody, {}, 0.8, 3), item("m2", TrackRole::Melody, {}, 0.8, 4),
                      item("d", TrackRole::Drums), item("c", TrackRole::Chords)});
    const auto examples = personalExamples(set, 50);
    REQUIRE(examples.size() == 3);
    CHECK(examples[0].role == TrackRole::Bass);   // b2: weight 5
    CHECK(examples[1].role == TrackRole::Melody); // m2: weight 4
    CHECK(examples[2].role == TrackRole::Melody); // m1: weight 3 beats b1: weight 2
    CHECK(personalExamples(set, 50, 2).size() == 2);
    CHECK(personalExamples(set, 50, 1).size() == 1);
    CHECK(personalExamples(set, 50, 1)[0].role == TrackRole::Bass);
    CHECK(personalExamples(set, 50, 0).empty());
}

TEST_CASE("examples skip inactive items and sets without bass or melody", "[reference-profile]") {
    auto off = item("b", TrackRole::Bass);
    off.active = false;
    CHECK(personalExamples(setOf({off, item("d", TrackRole::Drums), item("c", TrackRole::Chords)}), 50).empty());
    CHECK(personalExamples(ReferenceSet{}, 50).empty());
    auto empty = item("e", TrackRole::Bass);
    empty.notes.clear();
    CHECK(personalExamples(setOf({empty}), 50).empty());
}

TEST_CASE("equal weights are decided by the density closest to the energy, then by the id", "[reference-profile]") {
    auto sparse = item("a", TrackRole::Bass);
    sparse.analysis.rhythm.bars.assign(4, std::bitset<16>(0x0101)); // 2 of 16: 125 permille
    auto dense = item("b", TrackRole::Bass);
    dense.analysis.rhythm.bars.assign(4, std::bitset<16>(0xFFFF)); // 1000 permille
    auto middle = item("c", TrackRole::Bass);
    middle.analysis.rhythm.bars.assign(4, std::bitset<16>(0x7777)); // 12 of 16: 750 permille
    // the examples differ in the number of notes
    sparse.notes.resize(4);
    dense.notes.resize(8);
    middle.notes.resize(12);
    const auto pick = [&](int energy) { return personalExamples(setOf({sparse, dense, middle}), energy, 1)[0].notes.size(); };
    CHECK(pick(0) == 4);   // target 150: the sparse one
    CHECK(pick(50) == 12); // target 450: the middle one (300 away) beats the sparse (325) and the dense (550)
    CHECK(pick(100) == 12); // target 750: the middle one, exactly
    CHECK(pick(1000) == 12); // the energy is clamped to 100
}

TEST_CASE("an example is cut to four bars, with the steps and semitones from the key root", "[reference-profile]") {
    auto long_ = item("l", TrackRole::Bass);
    long_.lengthBars = 8;
    long_.notes.clear();
    // A (root 9) notes: 45 is the lowest, so the base is 45 and a note at 52 is 7 semitones up
    for (uint32_t bar = 0; bar < 8; ++bar) {
        MidiClipNote low;
        low.pitch = 45;
        low.startTick = bar * kTicksPerBar;
        low.lengthTicks = 480;
        long_.notes.push_back(low);
        MidiClipNote high;
        high.pitch = 52;
        high.startTick = bar * kTicksPerBar + 3 * 240 + 100; // 3 steps and a bit: step 3
        high.lengthTicks = 700;                              // 2.9 steps: 3
        long_.notes.push_back(high);
    }
    const auto examples = personalExamples(setOf({long_}), 50);
    REQUIRE(examples.size() == 1);
    const auto& example = examples[0];
    CHECK(example.bars == 4);
    CHECK(example.root == 9);
    CHECK(example.scaleId == "natural_minor");
    REQUIRE(example.notes.size() == 8); // four bars of two notes
    CHECK(example.notes[0] == ExampleNote{0, 0, 2});
    CHECK(example.notes[1] == ExampleNote{3, 7, 3});
    CHECK(example.notes[2].step == 16);
    CHECK(example.notes[7].step == 51); // bar 3, step 3
}

TEST_CASE("the base of the semitones is the root at or below the lowest note", "[reference-profile]") {
    auto bass = item("l", TrackRole::Bass);
    bass.lengthBars = 1;
    bass.notes.clear();
    MidiClipNote a;
    a.pitch = 50; // D: above the root A (45); the base is the A at or below 50, which is 45
    a.startTick = 0;
    a.lengthTicks = 240;
    bass.notes.push_back(a);
    MidiClipNote b;
    b.pitch = 57; // a octave of the root
    b.startTick = 480;
    b.lengthTicks = 240;
    bass.notes.push_back(b);
    const auto example = personalExamples(setOf({bass}), 50)[0];
    CHECK(example.notes[0].semitones == 5);
    CHECK(example.notes[1].semitones == 12);
    // a note below the root of the octave: the lowest is 43 (G), the root A at or below it is 33
    bass.notes[0].pitch = 43;
    const auto below = personalExamples(setOf({bass}), 50)[0];
    CHECK(below.notes[0].semitones == 10);
}

TEST_CASE("an example holds at most 64 notes, and a note at the end keeps at least one step", "[reference-profile]") {
    auto dense = item("d", TrackRole::Melody);
    dense.notes.clear();
    for (uint32_t i = 0; i < 100; ++i) {
        MidiClipNote note;
        note.pitch = 69;
        note.startTick = (i % 64) * 240;
        note.lengthTicks = 1; // rounds to 0 steps: still one
        dense.notes.push_back(note);
    }
    const auto example = personalExamples(setOf({dense}), 50)[0];
    CHECK(example.notes.size() == 64);
    for (const auto& note : example.notes) {
        CHECK(note.lengthSteps == 1);
    }
    // a start in the last half step of the fourth bar rounds to the end: it stays inside
    MidiClipNote late;
    late.pitch = 69;
    late.startTick = 4 * kTicksPerBar - 10;
    late.lengthTicks = 4000;
    dense.notes = {late};
    const auto edge = personalExamples(setOf({dense}), 50)[0];
    REQUIRE(edge.notes.size() == 1);
    CHECK(edge.notes[0].step == 63);
    CHECK(edge.notes[0].lengthSteps == 1);
}
