#include "PatternFixtures.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/Groove.h"
#include "core/Progression.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
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

GenerationRequest requestFor(uint32_t bars, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    return request;
}

int score(const Pattern& pattern, const StyleProfile& style, const GenerationRequest& request) {
    const QualityContext context = qualityContextFor(pattern, style, request.settings);
    return overallScore(scoreCriteria(pattern, context), style.quality, request.settings.creativityPct);
}

} // namespace

TEST_CASE("a candidate is a pure function of the seed", "[generator]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        std::set<size_t> distinct;
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            const GenerationRequest request = requestFor(4, 99);
            const Pattern a = generateCandidate(style, request, seed);
            const Pattern b = generateCandidate(style, request, seed);
            CHECK(a == b);
            distinct.insert(a.voices[0].notes.size() * 1000 + a.voices[1].notes.size() * 7 + a.context.root);
        }
        CHECK(distinct.size() > 3);
    }
}

TEST_CASE("a candidate carries harmony, groove, kick grid and generation info", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(2, 42);
    request.settings = ArchetypeSettings{70, 30};
    const Pattern p = generateCandidate(style, request, 7);
    CHECK(p.styleId == "peak_time");
    CHECK(p.lengthBars == 2);
    CHECK(p.kickGridId == style.kickDefault);
    CHECK(p.info.source == "algorithm");
    CHECK(p.info.seed == 42);
    CHECK(p.info.winnerSeed == 7);
    CHECK(p.info.styleProfileVersion == style.version);
    CHECK(p.info.energyPct == 70);
    CHECK(p.info.creativityPct == 30);
    CHECK(p.voices[0].groove.swing == style.bass.swingDefault);
    CHECK(p.voices[1].groove.swing == style.melody.swingDefault);
    CHECK_FALSE(p.voices[0].notes.empty());
    CHECK_FALSE(p.voices[1].notes.empty());
    CHECK_FALSE(p.voices[0].archetypeId.empty());
    CHECK_FALSE(p.voices[1].archetypeId.empty());
    CHECK(findScale(p.context.scaleId) != nullptr);
}

TEST_CASE("candidates are valid and untouched by the constraint layer for every style and length", "[generator]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : kLengths) {
            for (uint64_t seed = 1; seed <= 10; ++seed) {
                const Pattern p = generateCandidate(style, requestFor(bars, 1), seed);
                INFO(name << " bars " << bars << " seed " << seed);
                CHECK(validatePattern(p).empty());
                Pattern copy = p;
                CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
                CHECK(copy.voices[0].notes == p.voices[0].notes);
                CHECK(copy.voices[1].notes == p.voices[1].notes);
            }
        }
    }
}

TEST_CASE("generatePattern returns the best valid candidate with score and winner seed", "[generator]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (const uint32_t bars : kLengths) {
            const GenerationRequest request = requestFor(bars, 5 * bars);
            const SelectionResult result = generatePattern(style, request);
            INFO(name << " bars " << bars);
            REQUIRE(result.success);
            CHECK(result.score >= style.quality.minScore);
            CHECK(result.pattern.qualityScore == result.score);
            CHECK(result.pattern.info.winnerSeed == result.seed);
            CHECK(result.pattern.info.seed == request.seed);
            CHECK(validatePattern(result.pattern).empty());
            // the winner is one of the candidates of the selection
            bool known = false;
            for (int round = 0; round < result.rounds; ++round) {
                for (int index = 0; index < kCandidatesPerRound; ++index) {
                    known = known || candidateSeed(request.seed, round, index) == result.seed;
                }
            }
            CHECK(known);
            // generation is repeatable
            CHECK(generatePattern(style, request).pattern == result.pattern);
        }
    }
}

TEST_CASE("replaying the winner seed gives exactly the generated pattern", "[generator]") {
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 8; ++seed) {
            const GenerationRequest request = requestFor(4, seed);
            const SelectionResult result = generatePattern(style, request);
            REQUIRE(result.success);
            INFO(name << " seed " << seed);
            CHECK(replayWinner(style, request, result.seed) == result.pattern);
        }
    }
}

TEST_CASE("the quality score of the winner beats or ties every other valid candidate of its round", "[generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const GenerationRequest request = requestFor(4, 11);
    const SelectionResult result = generatePattern(style, request);
    REQUIRE(result.success);
    for (int index = 0; index < kCandidatesPerRound; ++index) {
        const Pattern other = generateCandidate(style, request, candidateSeed(request.seed, result.rounds - 1, index));
        CHECK(score(other, style, request) <= result.score);
    }
}

TEST_CASE("the selection fails rarely over seed series", "[generator]") {
    int failures = 0;
    int runs = 0;
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 60; ++seed) {
            const uint32_t bars = kLengths[seed % kLengths.size()];
            ++runs;
            failures += generatePattern(style, requestFor(bars, seed)).success ? 0 : 1;
        }
    }
    WARN("selection failures: " << failures << " of " << runs);
    CHECK(failures * 100 <= runs * 5);
}

TEST_CASE("fixed root, scale, kick grid and archetypes hold, the rest stays the same", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    for (uint64_t seed = 1; seed <= 10; ++seed) {
        const GenerationRequest free = requestFor(4, 3);
        GenerationRequest fixed = free;
        fixed.root = PitchClass{2};
        fixed.scaleId = "phrygian";
        fixed.kickGridId = "broken";
        fixed.bassArchetype = "offbeat";
        fixed.melodyArchetype = "arp";
        const Pattern a = generateCandidate(style, free, seed);
        const Pattern b = generateCandidate(style, fixed, seed);
        INFO("seed " << seed);
        CHECK(b.context.root == 2);
        CHECK(b.context.scaleId == "phrygian");
        CHECK(b.kickGridId == "broken");
        CHECK(b.voices[0].archetypeId == "offbeat");
        CHECK(b.voices[1].archetypeId == "arp");
        CHECK_FALSE(b.voices[0].archetypeAuto);
        CHECK(b.context.progression == a.context.progression);
        CHECK(validatePattern(b).empty());
    }
}

TEST_CASE("a manual archetype of the wrong role or an unknown one falls back to the automatic choice", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(2, 1);
    request.bassArchetype = "arp";
    request.melodyArchetype = "nonsense";
    const Pattern p = generateCandidate(style, request, 4);
    const Archetype* bass = findArchetype(p.voices[0].archetypeId);
    const Archetype* melody = findArchetype(p.voices[1].archetypeId);
    REQUIRE(bass != nullptr);
    REQUIRE(melody != nullptr);
    CHECK(bass->role == VoiceRole::Bass);
    CHECK(melody->role == VoiceRole::Melody);
}

TEST_CASE("a style that cannot be generated gives empty voices and no selection result", "[generator]") {
    StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(2, 1);
    request.scaleId = "no_such_scale";
    const Pattern p = generateCandidate(style, request, 1);
    CHECK(p.voices[0].notes.empty());
    CHECK(p.voices[1].notes.empty());
    CHECK_FALSE(generatePattern(style, request).success);
}

TEST_CASE("regenerateVoice replaces one voice and keeps harmony and the other voice", "[generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    const GenerationRequest request = requestFor(4, 8);
    const SelectionResult result = generatePattern(style, request);
    REQUIRE(result.success);
    for (uint64_t seed = 100; seed < 106; ++seed) {
        Pattern p = result.pattern;
        REQUIRE(regenerateVoice(p, 1, style, request, seed));
        INFO("seed " << seed);
        CHECK(p.context == result.pattern.context);
        CHECK(p.kickGridId == result.pattern.kickGridId);
        CHECK(p.voices[0].notes == result.pattern.voices[0].notes);
        CHECK(validatePattern(p).empty());
        CHECK(p.qualityScore == score(p, style, request));
        Pattern again = result.pattern;
        REQUIRE(regenerateVoice(again, 1, style, request, seed));
        CHECK(again == p);
        Pattern copy = p;
        CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
    }
    // another seed gives another line at least once
    Pattern a = result.pattern;
    Pattern b = result.pattern;
    REQUIRE(regenerateVoice(a, 1, style, request, 200));
    REQUIRE(regenerateVoice(b, 1, style, request, 201));
    CHECK_FALSE(a.voices[1].notes == b.voices[1].notes);
}

TEST_CASE("regenerating the bass keeps the melody within the rules against the new bass", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    const GenerationRequest request = requestFor(4, 21);
    const SelectionResult result = generatePattern(style, request);
    REQUIRE(result.success);
    for (uint64_t seed = 300; seed < 306; ++seed) {
        Pattern p = result.pattern;
        REQUIRE(regenerateVoice(p, 0, style, request, seed));
        CHECK(p.context == result.pattern.context);
        CHECK(validatePattern(p).empty());
        Pattern copy = p;
        CHECK(applyConstraints(copy, constraintSettingsFor(copy, style)).total() == 0);
    }
}

TEST_CASE("regenerateVoice refuses unknown and fully locked voices and leaves the pattern alone", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    const GenerationRequest request = requestFor(2, 1);
    Pattern p = generateCandidate(style, request, 3);
    const Pattern before = p;
    CHECK_FALSE(regenerateVoice(p, 2, style, request, 9));
    CHECK(p == before);
    p.voices[0].lock = LockFlags{true, true, true};
    const Pattern locked = p;
    CHECK_FALSE(regenerateVoice(p, 0, style, request, 9));
    CHECK(p == locked);
    // a partial lock does not stop the regeneration of the whole voice
    p.voices[0].lock = LockFlags{true, false, false};
    CHECK(regenerateVoice(p, 0, style, request, 9));
    // a pattern with an unknown scale cannot be regenerated
    Pattern broken = before;
    broken.context.scaleId = "no_such_scale";
    CHECK_FALSE(regenerateVoice(broken, 1, style, request, 9));
}

TEST_CASE("the copy protection rejects candidates that copy a distinctive reference entry", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(4, 6);
    const SelectionResult plain = generatePattern(style, request);
    REQUIRE(plain.success);
    // a set made of the voices of every candidate of the series
    for (int round = 0; round < kMaxRounds; ++round) {
        for (int index = 0; index < kCandidatesPerRound; ++index) {
            const Pattern candidate = generateCandidate(style, request, candidateSeed(request.seed, round, index));
            for (size_t voice = 0; voice < candidate.voices.size(); ++voice) {
                ReferenceEntry entry;
                entry.role = candidate.voices[voice].role;
                entry.bars = candidate.lengthBars;
                entry.notes = candidate.voices[voice].notes;
                request.referenceSet.push_back(entry);
            }
        }
    }
    REQUIRE(checkCopyProtection(plain.pattern, request.referenceSet).violated);
    // two candidates of the series have melodies too plain to be protected; the winner is one of them or none
    const SelectionResult protectedResult = generatePattern(style, request);
    CHECK(protectedResult.seed != plain.seed);
    if (protectedResult.success) {
        CHECK_FALSE(checkCopyProtection(protectedResult.pattern, request.referenceSet).violated);
    }
    // replaying a stored seed ignores the set (D-88)
    CHECK(replayWinner(style, request, plain.seed) == plain.pattern);
}

TEST_CASE("every voice of the pattern is generated, bass first", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(2, 1);
    Pattern p = generateCandidate(style, request, 5);
    REQUIRE(p.voices.size() == 2);
    CHECK(p.voices[0].role == VoiceRole::Bass);
    // the melody was built against this very bass: no interval rule is broken
    for (const Note& note : p.voices[1].notes) {
        CHECK_FALSE(violatesBassRules(p.voices[0], note.startTick, note.lengthTicks, note.pitch,
                                      style.chromaticDefaultPercent >= 30 || style.id == "hard_industrial"));
    }
}

TEST_CASE("a candidate equals the pipeline built by hand: harmony, groove, bass, melody, constraints", "[generator]") {
    StyleProfile style = loadShipped("melodic_techno");
    style.kickDefault = "broken";
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        GenerationRequest request = requestFor(4, 1);
        request.settings = ArchetypeSettings{55, 45};
        Pcg32 rng = Pcg32::fromSeed(seed);
        Pattern by = makeEmptyPattern(4, style.id);
        by.kickGridId = style.kickDefault;
        applyHarmony(by, style, rng);
        applyStyleGroove(by, style);
        for (size_t i = 0; i < 2; ++i) {
            const std::string id = resolveArchetype(by.voices[i], style, 55, rng, 45);
            REQUIRE(generateVoice(by, i, id, style, request.settings, rng));
        }
        applyConstraints(by, constraintSettingsFor(by, style));
        const Pattern p = generateCandidate(style, request, seed);
        INFO("seed " << seed);
        CHECK(p.kickGridId == "broken");
        CHECK(p.context == by.context);
        CHECK(p.voices[0].notes == by.voices[0].notes);
        CHECK(p.voices[1].notes == by.voices[1].notes);
        CHECK(p.voices[0].groove == by.voices[0].groove);
    }
}

TEST_CASE("the rating of every candidate uses its own archetypes (long_tied may hold over kicks)", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(4, 2);
    request.bassArchetype = "long_tied";
    const SelectionResult result = generatePattern(style, request);
    REQUIRE(result.success);
    CHECK(result.score == score(result.pattern, style, request));
}
