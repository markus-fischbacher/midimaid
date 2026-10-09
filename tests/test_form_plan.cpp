#include "PatternFixtures.h"
#include "core/Constraints.h"
#include "core/FormPlan.h"
#include "core/KickGrid.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"

#include <algorithm>
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

using R = PhraseRole;

std::string key(const std::vector<Phrase>& phrases) {
    std::string text;
    for (const Phrase& phrase : phrases) {
        text += std::to_string(phrase.lengthBars) + "/" + std::to_string(static_cast<int>(phrase.role)) + " ";
    }
    return text;
}

std::vector<Phrase> plan(std::initializer_list<std::pair<uint32_t, PhraseRole>> entries) {
    std::vector<Phrase> phrases;
    uint32_t start = 0;
    for (const auto& [bars, role] : entries) {
        Phrase phrase;
        phrase.startBar = start;
        phrase.lengthBars = bars;
        phrase.role = role;
        phrase.kickGridId = phraseKickGrid(role);
        phrases.push_back(phrase);
        start += bars;
    }
    return phrases;
}

GenerationRequest requestFor(uint32_t bars, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    return request;
}

size_t notesIn(const Track& track, const Phrase& phrase) {
    const uint32_t from = phrase.startBar * kTicksPerBar;
    const uint32_t to = from + phrase.lengthBars * kTicksPerBar;
    return static_cast<size_t>(std::count_if(track.notes.begin(), track.notes.end(), [&](const Note& note) {
        return note.startTick >= from && note.startTick < to;
    }));
}

double meanVelocity(const Pattern& p, const Phrase& phrase) {
    const uint32_t from = phrase.startBar * kTicksPerBar;
    const uint32_t to = from + phrase.lengthBars * kTicksPerBar;
    double sum = 0;
    double count = 0;
    for (const Track& track : p.voices) {
        for (const Note& note : track.notes) {
            if (note.startTick >= from && note.startTick < to) {
                sum += note.velocity;
                count += 1;
            }
        }
    }
    return count > 0 ? sum / count : 0;
}

} // namespace

TEST_CASE("patterns of up to 4 bars have one main phrase and draw nothing", "[formplan]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : {1u, 2u, 4u}) {
            Pcg32 rng = Pcg32::fromSeed(5);
            Pcg32 fresh = Pcg32::fromSeed(5);
            const auto phrases = generateFormPlan(style, bars, rng);
            REQUIRE(phrases.size() == 1);
            CHECK(phrases[0].startBar == 0);
            CHECK(phrases[0].lengthBars == bars);
            CHECK(phrases[0].role == R::Main);
            CHECK_FALSE(phrases[0].kickGridId.has_value());
            CHECK(rng.next() == fresh.next());
        }
    }
}

TEST_CASE("the layouts and roles of 8 and 16 bars follow the tables of the style", "[formplan]") {
    // keys are length/role with the enum values Main 0, Variation 1, Build 2, Breakdown 3, Answer 4
    const std::map<std::string, std::set<std::string>> expectedOther{
        {"8", {"8/0 ", "4/0 4/1 "}},
        {"16", {"16/0 ", "8/0 8/1 ", "8/0 4/1 4/2 ", "4/0 4/1 8/0 ", "4/0 8/1 4/2 ", "4/0 4/1 4/0 4/2 "}}};
    const std::map<std::string, std::set<std::string>> expectedMelodic{
        {"8", {"8/0 ", "4/0 4/4 "}},
        {"16", {"16/0 ", "8/0 8/4 ", "8/0 4/4 4/3 ", "4/0 4/4 8/0 ", "4/0 8/4 4/3 ", "4/0 4/4 4/0 4/3 "}}};
    CHECK(static_cast<int>(R::Main) == 0);
    CHECK(static_cast<int>(R::Variation) == 1);
    CHECK(static_cast<int>(R::Build) == 2);
    CHECK(static_cast<int>(R::Breakdown) == 3);
    CHECK(static_cast<int>(R::Answer) == 4);
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : {8u, 16u}) {
            std::set<std::string> seen;
            for (uint64_t seed = 1; seed <= 400; ++seed) {
                Pcg32 rng = Pcg32::fromSeed(seed);
                const auto phrases = generateFormPlan(style, bars, rng);
                INFO(name << " bars " << bars << " seed " << seed << ": " << key(phrases));
                CHECK(isValidFormPlan(bars, phrases));
                CHECK(phrases[0].role == R::Main);
                seen.insert(key(phrases));
                for (const Phrase& phrase : phrases) {
                    const bool breakdown = phrase.role == R::Breakdown;
                    CHECK(phrase.kickGridId.has_value() == breakdown);
                    if (breakdown) {
                        CHECK(*phrase.kickGridId == "halftime");
                        CHECK(name == "melodic_techno");
                    }
                    CHECK_FALSE(phrase.locked);
                    CHECK_FALSE(phrase.turnaround);
                }
            }
            const auto& table = name == "melodic_techno" ? expectedMelodic : expectedOther;
            // each layout of the table appears, and nothing else
            CHECK(seen == table.at(std::to_string(bars)));
        }
    }
}

TEST_CASE("layouts are drawn by the weights of the style", "[formplan]") {
    const StyleProfile peak = loadShipped("peak_time");
    const StyleProfile hard = loadShipped("hard_industrial");
    const int runs = 4000;
    int split = 0;
    int fourFour = 0;
    int hardSingle = 0;
    for (int i = 0; i < runs; ++i) {
        Pcg32 a = Pcg32::fromSeed(100 + static_cast<uint64_t>(i));
        split += generateFormPlan(peak, 8, a).size() == 2 ? 1 : 0; // 60 %
        Pcg32 b = Pcg32::fromSeed(100 + static_cast<uint64_t>(i));
        fourFour += generateFormPlan(peak, 16, b).size() == 4 ? 1 : 0; // 40 %
        Pcg32 c = Pcg32::fromSeed(100 + static_cast<uint64_t>(i));
        hardSingle += generateFormPlan(hard, 8, c).size() == 1 ? 1 : 0; // 60 %
    }
    int melodicSplit = 0;
    const StyleProfile melodic = loadShipped("melodic_techno");
    for (int i = 0; i < runs; ++i) {
        Pcg32 d = Pcg32::fromSeed(100 + static_cast<uint64_t>(i));
        melodicSplit += generateFormPlan(melodic, 8, d).size() == 2 ? 1 : 0; // 70 %
    }
    CHECK(melodicSplit > runs * 65 / 100);
    CHECK(melodicSplit < runs * 75 / 100);
    CHECK(split > runs * 55 / 100);
    CHECK(split < runs * 65 / 100);
    CHECK(fourFour > runs * 35 / 100);
    CHECK(fourFour < runs * 45 / 100);
    CHECK(hardSingle > runs * 55 / 100);
    CHECK(hardSingle < runs * 65 / 100);
}

TEST_CASE("isValidFormPlan applies the rules of the pattern validation", "[formplan]") {
    CHECK(isValidFormPlan(4, plan({{4, R::Main}})));
    CHECK(isValidFormPlan(16, plan({{4, R::Main}, {4, R::Variation}, {8, R::Build}})));
    CHECK_FALSE(isValidFormPlan(4, plan({{4, R::Variation}}))); // short patterns have one main phrase
    CHECK_FALSE(isValidFormPlan(8, plan({{4, R::Main}})));      // gap at the end
    CHECK_FALSE(isValidFormPlan(8, plan({{4, R::Main}, {2, R::Main}, {2, R::Main}}))); // phrase lengths 4, 8, 16
    CHECK_FALSE(isValidFormPlan(16, plan({{8, R::Main}, {4, R::Answer}})));
    CHECK_FALSE(isValidFormPlan(8, {}));
}

TEST_CASE("roles move energy, creativity and the kick grid", "[formplan]") {
    CHECK(phraseEnergyDelta(R::Main) == 0);
    CHECK(phraseEnergyDelta(R::Variation) == 0);
    CHECK(phraseEnergyDelta(R::Answer) == 0);
    CHECK(phraseEnergyDelta(R::Build) == 20);
    CHECK(phraseEnergyDelta(R::Breakdown) == -30);
    CHECK(phraseCreativityDelta(R::Variation) == 30);
    CHECK(phraseCreativityDelta(R::Main) == 0);
    CHECK(phraseCreativityDelta(R::Build) == 0);
    CHECK(phraseKickGrid(R::Breakdown) == std::optional<std::string>("halftime"));
    CHECK_FALSE(phraseKickGrid(R::Main).has_value());
    CHECK_FALSE(phraseKickGrid(R::Build).has_value());
}

TEST_CASE("candidates of 8 and 16 bars carry their form plan and satisfy all rules", "[formplan][generator]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : {8u, 16u}) {
            for (uint64_t seed = 1; seed <= 25; ++seed) {
                const Pattern p = generateCandidate(style, requestFor(bars, 1), seed);
                INFO(name << " bars " << bars << " seed " << seed << ": " << key(p.phrases));
                CHECK(isValidFormPlan(bars, p.phrases));
                CHECK(validatePattern(p).empty());
                CHECK_FALSE(p.voices[0].notes.empty());
                CHECK_FALSE(p.voices[1].notes.empty());
                CHECK_FALSE(p.voices[0].archetypeId.empty());
                CHECK_FALSE(p.voices[1].archetypeId.empty());
                Pattern copy = p;
                CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
                CHECK(copy.voices[0].notes == p.voices[0].notes);
                CHECK(copy.voices[1].notes == p.voices[1].notes);
                CHECK(generateCandidate(style, requestFor(bars, 1), seed) == p);
                // the notes are sorted and unique per id
                std::set<uint32_t> ids;
                for (const Track& track : p.voices) {
                    CHECK(std::is_sorted(track.notes.begin(), track.notes.end(),
                                         [](const Note& a, const Note& b) { return a.startTick < b.startTick; }));
                    for (const Note& note : track.notes) {
                        CHECK(ids.insert(note.id).second);
                    }
                }
            }
        }
    }
}

TEST_CASE("a fixed form plan replaces the draw, an invalid one is ignored", "[formplan][generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request = requestFor(16, 3);
    request.formPlan = plan({{4, R::Main}, {4, R::Answer}, {8, R::Build}});
    const Pattern p = generateCandidate(style, request, 9);
    CHECK(p.phrases == *request.formPlan);
    CHECK(p.context.progression == generateCandidate(style, requestFor(16, 3), 9).context.progression);
    request.formPlan = plan({{4, R::Main}}); // does not cover 16 bars
    CHECK(generateCandidate(style, request, 9) == generateCandidate(style, requestFor(16, 3), 9));
    // a plan for a short pattern must be the single main phrase
    GenerationRequest shortRequest = requestFor(4, 3);
    shortRequest.formPlan = plan({{4, R::Variation}});
    CHECK(generateCandidate(style, shortRequest, 9) == generateCandidate(style, requestFor(4, 3), 9));
}

TEST_CASE("a breakdown thins out, plays the halftime kick and a build gets louder", "[formplan][generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request = requestFor(12 + 4, 1);
    request.formPlan = plan({{4, R::Main}, {4, R::Build}, {4, R::Main}, {4, R::Breakdown}});
    request.settings = ArchetypeSettings{50, 40};
    size_t mainNotes = 0;
    size_t breakdownNotes = 0;
    size_t breakdownBeatTwoFour = 0; // free under halftime, a kick of the default grid
    double mainVelocity = 0;
    double buildVelocity = 0;
    const int runs = 30;
    for (int i = 1; i <= runs; ++i) {
        const Pattern p = generateCandidate(style, request, static_cast<uint64_t>(i));
        REQUIRE(p.phrases.size() == 4);
        for (const Track& track : p.voices) {
            mainNotes += notesIn(track, p.phrases[0]);
            breakdownNotes += notesIn(track, p.phrases[3]);
        }
        for (const Note& note : p.voices[0].notes) {
            const uint32_t step = (note.startTick / 240) % 16;
            if (note.startTick >= 12 * kTicksPerBar && (step == 4 || step == 12)) {
                ++breakdownBeatTwoFour;
            }
        }
        mainVelocity += meanVelocity(p, p.phrases[0]);
        buildVelocity += meanVelocity(p, p.phrases[1]);
        // the bass keeps off the halftime kicks (steps 0 and 8) in the breakdown, unless it holds over kicks
        if (!ignoresKickArchetype(p.voices[0].archetypeId)) {
            const auto kicks = kickTicks(p);
            const std::set<uint32_t> kickSet(kicks.begin(), kicks.end());
            for (const Note& note : p.voices[0].notes) {
                CHECK(kickSet.count(note.startTick) == 0);
            }
        }
    }
    CHECK(breakdownNotes < mainNotes);
    CHECK(breakdownBeatTwoFour > 0);
    CHECK(buildVelocity > mainVelocity);
}

TEST_CASE("phrases of the same role repeat their rhythm over the same chord", "[formplan][generator]") {
    StyleProfile style = loadShipped("peak_time");
    style.harmony.modes = {{"static", 1}}; // one chord for the whole pattern
    GenerationRequest request = requestFor(8, 4);
    request.formPlan = plan({{4, R::Main}, {4, R::Main}});
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        const Pattern p = generateCandidate(style, request, seed);
        for (const Track& track : p.voices) {
            std::vector<uint32_t> first;
            std::vector<uint32_t> second;
            for (const Note& note : track.notes) {
                (note.startTick < 4 * kTicksPerBar ? first : second).push_back(note.startTick % (4 * kTicksPerBar));
            }
            INFO("seed " << seed << " role " << static_cast<int>(track.role));
            CHECK(first == second);
        }
    }
    // a variation phrase has a seed of its own
    request.formPlan = plan({{4, R::Main}, {4, R::Variation}});
    int different = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        const Pattern p = generateCandidate(style, request, seed);
        std::vector<uint32_t> first;
        std::vector<uint32_t> second;
        for (const Note& note : p.voices[1].notes) {
            (note.startTick < 4 * kTicksPerBar ? first : second).push_back(note.startTick % (4 * kTicksPerBar));
        }
        different += first == second ? 0 : 1;
    }
    CHECK(different > 6);
}

TEST_CASE("the chords of a pattern reach every phrase", "[formplan][generator]") {
    StyleProfile style = loadShipped("melodic_techno");
    style.harmony.modes = {{"four_chord", 1}};
    GenerationRequest request = requestFor(16, 2);
    request.formPlan = plan({{4, R::Main}, {4, R::Variation}, {4, R::Main}, {4, R::Build}});
    for (uint64_t seed = 1; seed <= 8; ++seed) {
        const Pattern p = generateCandidate(style, request, seed);
        CHECK(validatePattern(p).empty());
        Pattern copy = p;
        CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
    }
}

TEST_CASE("regeneratePhrase replaces one phrase and keeps the rest", "[formplan][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(16, 7);
    request.formPlan = plan({{4, R::Main}, {4, R::Variation}, {4, R::Main}, {4, R::Build}});
    const SelectionResult result = generatePattern(style, request);
    REQUIRE(result.success);
    const Pattern& base = result.pattern;
    auto outside = [&](const Pattern& p, size_t voice, const Phrase& phrase) {
        std::vector<Note> kept;
        const uint32_t from = phrase.startBar * kTicksPerBar;
        const uint32_t to = from + phrase.lengthBars * kTicksPerBar;
        for (const Note& note : p.voices[voice].notes) {
            if (note.startTick < from || note.startTick >= to) {
                kept.push_back(note);
            }
        }
        return kept;
    };
    for (uint64_t seed = 50; seed < 54; ++seed) {
        Pattern p = base;
        REQUIRE(regeneratePhrase(p, 1, style, request, seed));
        INFO("seed " << seed);
        CHECK(p.context == base.context);
        CHECK(p.phrases == base.phrases);
        CHECK(validatePattern(p).empty());
        for (size_t voice = 0; voice < 2; ++voice) {
            CHECK(outside(p, voice, base.phrases[1]).size() == outside(base, voice, base.phrases[1]).size());
        }
        // the bass of the other phrases is the one of before (the melody may follow the new bass at the borders)
        for (const size_t other : {0u, 2u, 3u}) {
            CHECK(notesIn(p.voices[0], base.phrases[other]) == notesIn(base.voices[0], base.phrases[other]));
        }
        Pattern again = base;
        REQUIRE(regeneratePhrase(again, 1, style, request, seed));
        CHECK(again == p);
        Pattern copy = p;
        CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
    }
    Pattern a = base;
    Pattern b = base;
    REQUIRE(regeneratePhrase(a, 3, style, request, 71));
    REQUIRE(regeneratePhrase(b, 3, style, request, 72));
    CHECK_FALSE(a == b);
}

TEST_CASE("regeneratePhrase refuses unknown and locked phrases and voices without archetype", "[formplan][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(8, 2);
    request.formPlan = plan({{4, R::Main}, {4, R::Variation}});
    Pattern p = generateCandidate(style, request, 5);
    const Pattern before = p;
    CHECK_FALSE(regeneratePhrase(p, 2, style, request, 1));
    CHECK(p == before);
    p.phrases[1].locked = true;
    const Pattern locked = p;
    CHECK_FALSE(regeneratePhrase(p, 1, style, request, 1));
    CHECK(p == locked);
    CHECK(regeneratePhrase(p, 0, style, request, 1));
    Pattern empty = before;
    empty.voices[1].archetypeId.clear();
    CHECK_FALSE(regeneratePhrase(empty, 0, style, request, 1));
    Pattern broken = before;
    broken.context.scaleId = "no_such_scale";
    CHECK_FALSE(regeneratePhrase(broken, 0, style, request, 1));
}

TEST_CASE("regeneratePhrase keeps a fully locked voice and works for short patterns", "[formplan][generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const GenerationRequest request = requestFor(4, 2);
    Pattern p = generateCandidate(style, request, 6);
    p.voices[1].lock = LockFlags{true, true, true};
    const Pattern before = p;
    REQUIRE(regeneratePhrase(p, 0, style, request, 33));
    CHECK(p.voices[1].notes == before.voices[1].notes);
    CHECK_FALSE(p.voices[0].notes == before.voices[0].notes);
    CHECK(validatePattern(p).empty());
}

TEST_CASE("a phrase that cannot be generated leaves its voice without archetype", "[formplan][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(8, 1);
    request.scaleId = "no_such_scale";
    request.formPlan = plan({{4, R::Main}, {4, R::Variation}});
    const Pattern p = generateCandidate(style, request, 1);
    CHECK(p.voices[0].archetypeId.empty());
    CHECK(p.voices[1].archetypeId.empty());
    CHECK_FALSE(generatePattern(style, request).success);
}

TEST_CASE("a phrase takes the drum reference of the pattern into its generation", "[formplan][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(8, 3);
    request.formPlan = plan({{4, R::Main}, {4, R::Variation}});
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Pattern p = generateCandidate(style, request, seed);
        if (ignoresKickArchetype(p.voices[0].archetypeId)) {
            continue;
        }
        RhythmReference ref;
        ref.bars = 1;
        for (const size_t step : {0u, 6u, 10u}) {
            ref.kickSteps.set(step);
        }
        p.rhythmRef = ref;
        p.kickGridId = "custom";
        REQUIRE(regeneratePhrase(p, 1, style, request, seed + 40));
        const auto kicks = kickTicks(p);
        const std::set<uint32_t> kickSet(kicks.begin(), kicks.end());
        INFO("seed " << seed);
        for (const Note& note : p.voices[0].notes) {
            if (note.startTick >= 4 * kTicksPerBar) {
                CHECK(kickSet.count(note.startTick) == 0);
            }
        }
    }
}
