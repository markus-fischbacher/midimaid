#include "PatternFixtures.h"
#include "core/Archetype.h"
#include "core/ChordSymbol.h"
#include "core/Constraints.h"
#include "core/PatternValidation.h"
#include "core/Progression.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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
    INFO(name << ": " << result.error);
    REQUIRE(result.ok());
    return *result.profile;
}

const std::vector<std::string> kStyles{"peak_time", "melodic_techno", "hard_industrial"};
const std::vector<uint32_t> kLengths{1, 2, 4, 8, 16};

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

HarmonyProfile harmony(std::vector<std::vector<Chord>> progressions, int minBars, int maxBars) {
    HarmonyProfile h;
    h.progressions = std::move(progressions);
    h.chordLengthBarsMin = minBars;
    h.chordLengthBarsMax = maxBars;
    return h;
}

StyleProfile styleWith(HarmonyProfile h, std::vector<WeightedId> modes) {
    StyleProfile style;
    style.harmony = std::move(h);
    style.harmony.modes = std::move(modes);
    style.scales = {{"natural_minor", 1}};
    return style;
}

/// Pattern with the context only, for the validator.
bool contextValid(const StyleProfile& style, uint32_t bars, const std::vector<ChordEvent>& events) {
    Pattern p = makeEmptyPattern(bars, "x");
    p.context.progression = events;
    for (const std::string& issue : validatePattern(p)) {
        if (issue.find("context") != std::string::npos) {
            return false;
        }
    }
    (void)style;
    return true;
}

} // namespace

TEST_CASE("progressionsForMode: static is the minor tonic, unknown modes have none", "[progression]") {
    const HarmonyProfile h = harmony({chords({"i", "iv"})}, 4, 8);
    const auto stat = progressionsForMode(h, "static");
    REQUIRE(stat.size() == 1);
    CHECK(stat[0] == chords({"i"}));
    CHECK(progressionsForMode(h, "three_chord").empty());
    CHECK(progressionsForMode(h, "").empty());
}

TEST_CASE("progressionsForMode: exact matches win over derived progressions (O-25)", "[progression]") {
    const HarmonyProfile h =
        harmony({chords({"i", "iv"}), chords({"i", "bVI", "bIII", "bVII"}), chords({"i", "V"})}, 1, 2);
    const auto two = progressionsForMode(h, "two_chord");
    REQUIRE(two.size() == 2);
    CHECK(two[0] == chords({"i", "iv"}));
    CHECK(two[1] == chords({"i", "V"}));
    const auto four = progressionsForMode(h, "four_chord");
    REQUIRE(four.size() == 1);
    CHECK(four[0] == chords({"i", "bVI", "bIII", "bVII"}));
}

TEST_CASE("progressionsForMode derives the missing lengths (O-25)", "[progression]") {
    // Peak Time: only two-chord progressions
    const StyleProfile peak = loadShipped("peak_time");
    const auto four = progressionsForMode(peak.harmony, "four_chord");
    REQUIRE(four.size() == 3);
    CHECK(four[0] == chords({"i", "bVII", "i", "bVII"}));
    CHECK(four[1] == chords({"i", "bVI", "i", "bVI"}));
    CHECK(four[2] == chords({"i", "iv", "i", "iv"}));
    // Melodic Techno: only four-chord progressions
    const StyleProfile melodic = loadShipped("melodic_techno");
    const auto two = progressionsForMode(melodic.harmony, "two_chord");
    REQUIRE(two.size() == 4);
    CHECK(two[0] == chords({"i", "bVI"}));
    CHECK(two[1] == chords({"i", "iv"}));
    CHECK(two[2] == chords({"i", "bVII"}));
    CHECK(two[3] == chords({"i", "bVI"}));
    // three chords
    const HarmonyProfile three = harmony({chords({"i", "iv", "V"})}, 1, 2);
    const auto extended = progressionsForMode(three, "four_chord");
    REQUIRE(extended.size() == 1);
    CHECK(extended[0] == chords({"i", "iv", "V", "iv"}));
    const auto cut = progressionsForMode(three, "two_chord");
    REQUIRE(cut.size() == 1);
    CHECK(cut[0] == chords({"i", "iv"}));
    // nothing to derive from
    CHECK(progressionsForMode(harmony({}, 1, 2), "two_chord").empty());
    CHECK(progressionsForMode(harmony({}, 1, 2), "four_chord").empty());
}

TEST_CASE("fittingChordLengths keeps the profile range where the progression fits", "[progression]") {
    const HarmonyProfile peak = harmony({}, 4, 8);
    CHECK(fittingChordLengths(peak, 2, 16) == std::vector<uint32_t>{4, 8});
    CHECK(fittingChordLengths(peak, 2, 8) == std::vector<uint32_t>{4});
    CHECK(fittingChordLengths(peak, 4, 16) == std::vector<uint32_t>{4});
    // below the profile minimum when nothing in the range fits
    CHECK(fittingChordLengths(peak, 2, 4) == std::vector<uint32_t>{2});
    CHECK(fittingChordLengths(peak, 2, 2) == std::vector<uint32_t>{1});
    CHECK(fittingChordLengths(peak, 4, 8) == std::vector<uint32_t>{2});
    CHECK(fittingChordLengths(peak, 4, 4) == std::vector<uint32_t>{1});
    CHECK(fittingChordLengths(peak, 2, 1) == std::vector<uint32_t>{1});
    const HarmonyProfile melodic = harmony({}, 1, 2);
    CHECK(fittingChordLengths(melodic, 4, 16) == std::vector<uint32_t>{1, 2});
    CHECK(fittingChordLengths(melodic, 4, 8) == std::vector<uint32_t>{1, 2});
    CHECK(fittingChordLengths(melodic, 4, 4) == std::vector<uint32_t>{1});
    CHECK(fittingChordLengths(melodic, 2, 2) == std::vector<uint32_t>{1});
    // the range is cut to standard lengths; 3 and 5 do not exist
    CHECK(fittingChordLengths(harmony({}, 3, 5), 2, 16) == std::vector<uint32_t>{4});
}

TEST_CASE("static mode gives one tonic chord over the whole pattern", "[progression]") {
    const StyleProfile style = styleWith(harmony({chords({"i", "iv"})}, 4, 8), {{"static", 1}});
    for (const uint32_t bars : kLengths) {
        Pcg32 rng = Pcg32::fromSeed(bars);
        const auto events = generateProgression(style, bars, rng);
        REQUIRE(events.size() == 1);
        CHECK(events[0].chord == chord("i"));
        CHECK(events[0].startHalfBar == 0);
        CHECK(events[0].lengthHalfBars == bars * 2);
    }
}

TEST_CASE("a profile without modes or progressions falls back to static", "[progression]") {
    StyleProfile style = styleWith(harmony({}, 4, 8), {});
    Pcg32 rng = Pcg32::fromSeed(1);
    auto events = generateProgression(style, 4, rng);
    REQUIRE(events.size() == 1);
    CHECK(events[0].lengthHalfBars == 8);
    // modes only with zero weight or unknown ids
    style = styleWith(harmony({chords({"i", "iv"})}, 4, 8), {{"two_chord", 0}, {"nonsense", 5}});
    events = generateProgression(style, 8, rng);
    REQUIRE(events.size() == 1);
    // a changing mode without any progression
    style = styleWith(harmony({}, 4, 8), {{"four_chord", 1}});
    events = generateProgression(style, 8, rng);
    REQUIRE(events.size() == 1);
    CHECK(events[0].chord == chord("i"));
}

TEST_CASE("two chords split the pattern by the chord length", "[progression]") {
    const StyleProfile style = styleWith(harmony({chords({"i", "bVII"})}, 4, 8), {{"two_chord", 1}});
    // 4 bars: 2 + 2 bars (below the profile minimum, because 4 + 4 does not fit)
    Pcg32 rng = Pcg32::fromSeed(7);
    auto events = generateProgression(style, 4, rng);
    REQUIRE(events.size() == 2);
    CHECK(events[0].chord == chord("i"));
    CHECK(events[0].startHalfBar == 0);
    CHECK(events[0].lengthHalfBars == 4);
    CHECK(events[1].chord == chord("bVII"));
    CHECK(events[1].startHalfBar == 4);
    CHECK(events[1].lengthHalfBars == 4);
    // 16 bars: 4 + 4 repeated twice, or 8 + 8
    std::set<size_t> counts;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pcg32 r = Pcg32::fromSeed(seed);
        events = generateProgression(style, 16, r);
        counts.insert(events.size());
        const uint32_t length = events[0].lengthHalfBars;
        CHECK((length == 8 || length == 16));
        for (size_t i = 0; i < events.size(); ++i) {
            CHECK(events[i].chord == chord(i % 2 == 0 ? "i" : "bVII"));
            CHECK(events[i].lengthHalfBars == length);
            CHECK(events[i].startHalfBar == i * length);
        }
    }
    CHECK(counts == std::set<size_t>{2, 4});
}

TEST_CASE("a pattern of one bar has half-bar chords, at most two", "[progression]") {
    const StyleProfile two = styleWith(harmony({chords({"i", "iv"})}, 4, 8), {{"two_chord", 1}});
    Pcg32 rng = Pcg32::fromSeed(3);
    auto events = generateProgression(two, 1, rng);
    REQUIRE(events.size() == 2);
    CHECK(events[0].chord == chord("i"));
    CHECK(events[0].lengthHalfBars == 1);
    CHECK(events[1].chord == chord("iv"));
    CHECK(events[1].startHalfBar == 1);
    CHECK(events[1].lengthHalfBars == 1);
    const StyleProfile four = styleWith(harmony({chords({"i", "bVI", "bIII", "bVII"})}, 1, 2), {{"four_chord", 1}});
    events = generateProgression(four, 1, rng);
    REQUIRE(events.size() == 2);
    CHECK(events[0].chord == chord("i"));
    CHECK(events[1].chord == chord("bVI"));
}

TEST_CASE("four chords cycle through the pattern", "[progression]") {
    const StyleProfile style = styleWith(harmony({chords({"i", "bVI", "bIII", "bVII"})}, 1, 2), {{"four_chord", 1}});
    std::set<uint32_t> lengths;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        const auto events = generateProgression(style, 8, rng);
        const uint32_t length = events[0].lengthHalfBars;
        lengths.insert(length);
        REQUIRE((length == 2 || length == 4));
        REQUIRE(events.size() == 16 / length);
        const char* order[] = {"i", "bVI", "bIII", "bVII"};
        for (size_t i = 0; i < events.size(); ++i) {
            CHECK(events[i].chord == chord(order[i % 4]));
        }
    }
    CHECK(lengths == std::set<uint32_t>{2, 4});
}

TEST_CASE("generated progressions are gapless and valid for every style, length and seed", "[progression]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        std::set<std::string> allowed;
        for (const auto& mode : {"static", "two_chord", "four_chord"}) {
            for (const auto& progression : progressionsForMode(style.harmony, mode)) {
                for (const Chord& c : progression) {
                    allowed.insert(formatChordSymbol(c));
                }
            }
        }
        for (const uint32_t bars : kLengths) {
            for (uint64_t seed = 1; seed <= 60; ++seed) {
                Pcg32 rng = Pcg32::fromSeed(seed);
                const auto events = generateProgression(style, bars, rng);
                INFO(name << " bars " << bars << " seed " << seed);
                REQUIRE_FALSE(events.empty());
                CHECK(contextValid(style, bars, events));
                uint32_t end = 0;
                for (const ChordEvent& event : events) {
                    CHECK(event.startHalfBar == end);
                    CHECK(event.lengthHalfBars >= 1);
                    CHECK(allowed.count(formatChordSymbol(event.chord)) == 1);
                    end += event.lengthHalfBars;
                }
                CHECK(end == bars * 2);
            }
        }
    }
}

TEST_CASE("generation is deterministic and seed dependent", "[progression]") {
    const StyleProfile style = loadShipped("melodic_techno");
    std::set<std::string> seen;
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pcg32 a = Pcg32::fromSeed(seed);
        Pcg32 b = Pcg32::fromSeed(seed);
        const HarmonicContext first = makeHarmonicContext(style, 8, a);
        const HarmonicContext second = makeHarmonicContext(style, 8, b);
        CHECK(first == second);
        CHECK(a.next() == b.next());
        std::string key = first.scaleId + std::to_string(first.root);
        for (const ChordEvent& event : first.progression) {
            key += formatChordSymbol(event.chord) + std::to_string(event.lengthHalfBars);
        }
        seen.insert(key);
    }
    CHECK(seen.size() > 20);
}

TEST_CASE("modes are drawn by their weights", "[progression]") {
    const StyleProfile style = loadShipped("peak_time"); // static 50, two_chord 40, four_chord 10
    const int runs = 4000;
    int single = 0;
    int four = 0;
    for (int i = 0; i < runs; ++i) {
        Pcg32 rng = Pcg32::fromSeed(1000 + static_cast<uint64_t>(i));
        const size_t events = generateProgression(style, 16, rng).size();
        single += events == 1 ? 1 : 0;
        four += events == 4 ? 1 : 0;
    }
    // static is one event. Four events come from four_chord (10 %) and from two_chord with 4-bar chords (half of 40 %).
    CHECK(single > runs * 45 / 100);
    CHECK(single < runs * 55 / 100);
    CHECK(four > runs * 26 / 100);
    CHECK(four < runs * 34 / 100);
}

TEST_CASE("scales are drawn by the weights of the style, the root uniformly", "[progression]") {
    const StyleProfile style = loadShipped("peak_time"); // 3, 2, 2, 1
    std::map<std::string, int> scales;
    std::map<int, int> roots;
    const int runs = 4000;
    for (int i = 0; i < runs; ++i) {
        Pcg32 rng = Pcg32::fromSeed(5000 + static_cast<uint64_t>(i));
        const HarmonicContext context = makeHarmonicContext(style, 4, rng);
        scales[context.scaleId]++;
        roots[context.root]++;
        CHECK(findScale(context.scaleId) != nullptr);
    }
    CHECK(scales.size() == 4);
    CHECK(scales["natural_minor"] > runs * 33 / 100); // 37.5 %
    CHECK(scales["natural_minor"] < runs * 42 / 100);
    CHECK(scales["minor_pentatonic"] > runs * 9 / 100); // 12.5 %
    CHECK(scales["minor_pentatonic"] < runs * 16 / 100);
    CHECK(roots.size() == 12);
    for (const auto& [root, count] : roots) {
        CHECK(count > runs / 12 * 80 / 100);
        CHECK(count < runs / 12 * 120 / 100);
    }
}

TEST_CASE("a given root or scale replaces the draw without changing the rest", "[progression]") {
    const StyleProfile style = loadShipped("melodic_techno");
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        Pcg32 a = Pcg32::fromSeed(seed);
        const HarmonicContext free = makeHarmonicContext(style, 8, a);
        Pcg32 b = Pcg32::fromSeed(seed);
        const HarmonicContext fixedRoot = makeHarmonicContext(style, 8, b, PitchClass{3});
        Pcg32 c = Pcg32::fromSeed(seed);
        const HarmonicContext fixedScale = makeHarmonicContext(style, 8, c, std::nullopt, std::string("locrian"));
        CHECK(fixedRoot.root == 3);
        CHECK(fixedRoot.scaleId == free.scaleId);
        CHECK(fixedRoot.progression == free.progression);
        CHECK(fixedScale.root == free.root);
        CHECK(fixedScale.scaleId == "locrian");
        CHECK(fixedScale.progression == free.progression);
        const uint32_t next = a.next();
        CHECK(b.next() == next);
        CHECK(c.next() == next);
    }
}

TEST_CASE("a style without scales keeps the default scale", "[progression]") {
    StyleProfile style = styleWith(harmony({chords({"i", "iv"})}, 4, 8), {{"static", 1}});
    style.scales.clear();
    Pcg32 rng = Pcg32::fromSeed(1);
    CHECK(makeHarmonicContext(style, 4, rng).scaleId == "natural_minor");
}

TEST_CASE("applyHarmony replaces the context and keeps the pattern valid", "[progression]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : kLengths) {
            Pattern p = makeEmptyPattern(bars, name);
            Pcg32 rng = Pcg32::fromSeed(bars * 31);
            applyHarmony(p, style, rng, PitchClass{5}, std::string("dorian"));
            INFO(name << " bars " << bars);
            CHECK(p.context.root == 5);
            CHECK(p.context.scaleId == "dorian");
            CHECK(validatePattern(p).empty());
        }
    }
}

TEST_CASE("the generated harmony drives bass and melody without constraint changes", "[progression][archetype]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : kLengths) {
            for (uint64_t seed = 1; seed <= 8; ++seed) {
                Pattern p = makeEmptyPattern(bars, name);
                Pcg32 rng = Pcg32::fromSeed(seed * 97 + bars);
                applyHarmony(p, style, rng);
                const ArchetypeSettings settings{60, 40};
                const std::string bassId =
                    resolveArchetype(p.voices[0], style, settings.energyPct, rng, settings.creativityPct);
                const std::string melodyId =
                    resolveArchetype(p.voices[1], style, settings.energyPct, rng, settings.creativityPct);
                REQUIRE(generateVoice(p, 0, bassId, style, settings, rng));
                REQUIRE(generateVoice(p, 1, melodyId, style, settings, rng));
                INFO(name << " bars " << bars << " seed " << seed << " " << bassId << " " << melodyId);
                CHECK(validatePattern(p).empty());
                Pattern copy = p;
                const ConstraintReport report = applyConstraints(copy, constraintSettingsFor(copy, style));
                CHECK(report.total() == 0);
                CHECK(copy.voices[0].notes == p.voices[0].notes);
                CHECK(copy.voices[1].notes == p.voices[1].notes);
            }
        }
    }
}

TEST_CASE("the progression is drawn among all candidates of the mode", "[progression]") {
    const StyleProfile peak = loadShipped("peak_time"); // two-chord progressions i-bVII, i-bVI, i-iv
    std::set<std::string> seconds;
    for (uint64_t seed = 1; seed <= 100; ++seed) {
        StyleProfile style = peak;
        style.harmony.modes = {{"two_chord", 1}};
        Pcg32 rng = Pcg32::fromSeed(seed);
        seconds.insert(formatChordSymbol(generateProgression(style, 16, rng).at(1).chord));
    }
    CHECK(seconds == std::set<std::string>{"bVII", "bVI", "iv"});
}

TEST_CASE("unknown modes are never drawn, even with weight", "[progression]") {
    const StyleProfile style = styleWith(harmony({chords({"i", "iv"})}, 4, 8), {{"nonsense", 50}, {"two_chord", 1}});
    for (uint64_t seed = 1; seed <= 40; ++seed) {
        Pcg32 rng = Pcg32::fromSeed(seed);
        CHECK(generateProgression(style, 8, rng).size() == 2);
    }
}

TEST_CASE("a pattern of two bars has one-bar chords, an empty pattern one minimal chord", "[progression]") {
    const StyleProfile style = styleWith(harmony({chords({"i", "iv"})}, 4, 8), {{"two_chord", 1}});
    Pcg32 rng = Pcg32::fromSeed(2);
    const auto events = generateProgression(style, 2, rng);
    REQUIRE(events.size() == 2);
    CHECK(events[0].lengthHalfBars == 2);
    CHECK(events[1].startHalfBar == 2);
    CHECK(events[1].lengthHalfBars == 2);
    const auto empty = generateProgression(style, 0, rng);
    REQUIRE(empty.size() == 1);
    CHECK(empty[0].lengthHalfBars == 1);
}
