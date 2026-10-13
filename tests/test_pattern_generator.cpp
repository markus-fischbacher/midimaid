#include "PatternFixtures.h"
#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/Groove.h"
#include "core/PatternEdit.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/Progression.h"

#include <atomic>
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

TEST_CASE("generation around locks protects against the set for the generated voices only", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(4, 6);
    // a locked bass that is distinctive, so that the set would flag it if the check looked at it
    Pattern current;
    ReferenceEntry ownBass;
    for (uint64_t seed = 1; seed <= 60; ++seed) {
        current = generateCandidate(style, request, seed);
        ownBass.role = VoiceRole::Bass;
        ownBass.bars = current.lengthBars;
        ownBass.notes = current.voices[0].notes;
        if (isDistinctive(ownBass)) {
            break;
        }
    }
    REQUIRE(isDistinctive(ownBass));
    request.referenceSet = {ownBass};
    REQUIRE(checkCopyProtection(current, request.referenceSet).violated); // unlocked it is flagged
    current.voices[0].lock = LockFlags{true, true, true};
    const SelectionResult kept = generatePatternAroundLocks(style, request, current);
    CHECK(kept.success); // the locked bass is the musician's own: it never counts as a copy
    CHECK(kept.pattern.voices[0].notes == current.voices[0].notes);

    // the melody that the generation made without a set is in the set now: it must not come again
    GenerationRequest guardedRequest = requestFor(4, 6);
    // a line that can be distinctive: a polymeter cell or a plain repeated line is never one, so the seed is searched
    SelectionResult plain;
    ReferenceEntry melody;
    melody.role = VoiceRole::Melody;
    for (uint64_t seed = 6; seed < 60; ++seed) {
        guardedRequest.seed = seed;
        plain = generatePatternAroundLocks(style, guardedRequest, current);
        REQUIRE(plain.success);
        melody.bars = plain.pattern.lengthBars;
        melody.notes = plain.pattern.voices[1].notes;
        if (isDistinctive(melody)) {
            break;
        }
    }
    REQUIRE(isDistinctive(melody));
    guardedRequest.referenceSet = {melody};
    const SelectionResult guarded = generatePatternAroundLocks(style, guardedRequest, current);
    CHECK(guarded.seed != plain.seed);
    if (guarded.success) {
        CHECK_FALSE(checkCopyProtection(guarded.pattern, guardedRequest.referenceSet).violated);
    }
}

TEST_CASE("the last results of the slot are the history of the repetition protection", "[generator][repeat]") {
    const StyleProfile style = loadShipped("peak_time");
    std::vector<Pattern> results;
    for (uint64_t seed = 1; seed <= 25; ++seed) {
        results.push_back(generateCandidate(style, requestFor(2, 1), seed));
    }
    const auto entries = historyEntriesOf(results);
    // the newest 20 patterns, every voice with notes as an entry of its role
    size_t expected = 0;
    for (size_t index = results.size() - kRepeatHistory; index < results.size(); ++index) {
        for (const Track& track : results[index].voices) {
            expected += track.notes.empty() ? 0 : 1;
        }
    }
    CHECK(entries.size() == expected);
    REQUIRE_FALSE(entries.empty());
    CHECK(entries.back().notes == results.back().voices.back().notes);
    CHECK(entries.back().role == results.back().voices.back().role);
    CHECK(entries.back().bars == results.back().lengthBars);
    CHECK(entries.back().active);
    const auto oldest = results.size() - kRepeatHistory;
    CHECK(entries.front().notes == results[oldest].voices.front().notes);
    CHECK(historyEntriesOf({}).empty());
    // a voice without notes gives no entry
    Pattern bare = results.front();
    bare.voices[1].notes.clear();
    CHECK(historyEntriesOf({bare}).size() == 1);
}

TEST_CASE("a new pattern does not repeat a result of the history", "[generator][repeat]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(4, 6);
    const SelectionResult first = generatePattern(style, request);
    REQUIRE(first.success);
    request.history = historyEntriesOf({first.pattern});
    REQUIRE(checkCopyProtection(first.pattern, request.history).violated); // the same seed would give it again
    const SelectionResult again = generatePattern(style, request);
    REQUIRE(again.success);
    CHECK(again.seed != first.seed);
    CHECK_FALSE(checkCopyProtection(again.pattern, request.history).violated);
    // the winner seed replays the result whatever the history says (D-88)
    CHECK(replayWinner(style, request, again.seed) == again.pattern);
    CHECK(replayWinner(style, request, first.seed) == first.pattern);
}

TEST_CASE("the repetition protection never blocks a generation", "[generator][repeat]") {
    // look for a request whose candidates are all protected by a history made of them (some candidates have lines too
    // plain to be protected, D-87): then the protection must be dropped and the result is the unprotected one
    bool found = false;
    for (const char* styleName : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile style = loadShipped(styleName);
        for (uint64_t seed = 1; seed <= 60 && !found; ++seed) {
            GenerationRequest request = requestFor(seed % 2 == 0 ? 8 : 16, seed);
            request.voices = {VoiceRole::Melody}; // one distinctive line: every candidate can be protected
            const SelectionResult plain = generatePattern(style, request);
            if (!plain.success) {
                continue;
            }
            std::vector<Pattern> all;
            for (int round = 0; round < kMaxRounds; ++round) {
                for (int index = 0; index < kCandidatesPerRound; ++index) {
                    all.push_back(generateCandidate(style, request, candidateSeed(request.seed, round, index)));
                }
            }
            std::vector<ReferenceEntry> history; // by hand: `historyEntriesOf` keeps only the newest 20
            for (const Pattern& candidate : all) {
                for (const Track& track : candidate.voices) {
                    history.push_back({track.role, candidate.lengthBars, track.notes, true});
                }
            }
            const bool allBlocked = std::all_of(all.begin(), all.end(), [&](const Pattern& candidate) {
                return checkCopyProtection(candidate, history).violated;
            });
            if (!allBlocked) {
                continue;
            }
            found = true;
            request.history = history;
            const SelectionResult result = generatePattern(style, request);
            REQUIRE(result.success);
            CHECK(result.pattern == plain.pattern);
            CHECK(result.seed == plain.seed);
            // a cancelled request still ends without a result and without a second run
            std::atomic<bool> cancel{true};
            request.cancel = &cancel;
            CHECK_FALSE(generatePattern(style, request).success);
        }
    }
    CHECK(found);
}

TEST_CASE("the distance to the history is 100 minus the highest similarity", "[generator][repeat]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern first = generateCandidate(style, requestFor(4, 6), 11);
    const auto history = historyEntriesOf({first});
    REQUIRE(checkCopyProtection(first, history).permille == 1000);
    CHECK(historyDistanceScore(first, history) == 0); // the same pattern
    const Pattern other = generateCandidate(style, requestFor(4, 6), 12);
    const int permille = checkCopyProtection(other, history).permille;
    CHECK(historyDistanceScore(other, history) == 100 - permille / 10);
    CHECK(historyDistanceScore(other, history) > 0);
    CHECK(historyDistanceScore(first, {}) == 100); // nothing to compare
    auto inactive = history;
    for (auto& entry : inactive) {
        entry.active = false;
    }
    CHECK(historyDistanceScore(first, inactive) == 100);
}

TEST_CASE("the soft score has the criterion only with a history", "[generator][repeat]") {
    const StyleProfile style = loadShipped("peak_time");
    const Pattern pattern = generateCandidate(style, requestFor(4, 6), 11);
    QualityContext context;
    SoftScores scores = scoreCriteria(pattern, context);
    CHECK(scores.historyDistance == 100);
    CHECK(scores.historyDistancePct == 0);
    const std::vector<ReferenceEntry> none;
    context.history = &none;
    CHECK(scoreCriteria(pattern, context).historyDistancePct == 0);
    const auto history = historyEntriesOf({pattern});
    context.history = &history;
    scores = scoreCriteria(pattern, context);
    CHECK(scores.historyDistancePct == 200);
    CHECK(scores.historyDistance == 0);
}

TEST_CASE("a heavy weight on the distance chooses winners that are farther from the history", "[generator][repeat]") {
    StyleProfile light = loadShipped("peak_time");
    light.quality.historyDistance = 0;
    StyleProfile heavy = light;
    heavy.quality.historyDistance = 40;
    int farther = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        GenerationRequest request = requestFor(4, seed);
        std::vector<Pattern> results;
        for (uint64_t earlier = 100; earlier < 106; ++earlier) {
            results.push_back(generateCandidate(light, requestFor(4, earlier), earlier));
        }
        request.history = historyEntriesOf(results);
        const SelectionResult plain = generatePattern(light, request);
        const SelectionResult careful = generatePattern(heavy, request);
        REQUIRE((plain.success && careful.success));
        CHECK(careful.scores.historyDistancePct == 200);
        CHECK(careful.scores.historyDistance >= plain.scores.historyDistance);
        CHECK_FALSE(checkCopyProtection(careful.pattern, request.history).violated);
        farther += careful.scores.historyDistance > plain.scores.historyDistance ? 1 : 0;
    }
    CHECK(farther >= 1);
}

TEST_CASE("the stored rating of the winner does not depend on the history", "[generator][repeat]") {
    StyleProfile style = loadShipped("peak_time");
    style.quality.historyDistance = 40;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        GenerationRequest request = requestFor(4, seed);
        std::vector<Pattern> results;
        for (uint64_t earlier = 100; earlier < 104; ++earlier) {
            results.push_back(generateCandidate(style, requestFor(4, earlier), earlier));
        }
        request.history = historyEntriesOf(results);
        const SelectionResult result = generatePattern(style, request);
        REQUIRE(result.success);
        const Pattern replay = replayWinner(style, request, result.seed);
        CHECK(replay == result.pattern);
        CHECK(replay.qualityScore == result.pattern.qualityScore);
    }
}

TEST_CASE("without a history the new criterion changes nothing", "[generator][repeat]") {
    StyleProfile light = loadShipped("peak_time");
    light.quality.historyDistance = 0;
    StyleProfile heavy = light;
    heavy.quality.historyDistance = 40;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        const GenerationRequest request = requestFor(4, seed);
        const SelectionResult a = generatePattern(light, request);
        const SelectionResult b = generatePattern(heavy, request);
        REQUIRE((a.success && b.success));
        CHECK(a.seed == b.seed);
        CHECK(a.score == b.score);
        CHECK(b.scores.historyDistancePct == 0);
    }
}

TEST_CASE("generation around locks avoids the history for the generated voices", "[generator][repeat]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestFor(4, 6);
    request.melodyArchetype = "lead_phrase"; // a line that can be distinctive (a polymeter cell never is)
    Pattern current = generateCandidate(style, request, 3);
    current.voices[0].lock = LockFlags{true, true, true};
    const SelectionResult plain = generatePatternAroundLocks(style, request, current);
    REQUIRE(plain.success);
    request.history = historyEntriesOf({plain.pattern});
    const SelectionResult again = generatePatternAroundLocks(style, request, current);
    REQUIRE(again.success);
    CHECK(again.seed != plain.seed);
    CHECK(again.pattern.voices[0].notes == current.voices[0].notes); // the locked voice stays, it is never "repeated"
    CHECK_FALSE(checkCopyProtection(again.pattern, request.history).violated);
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

TEST_CASE("a set cancel flag ends the selection without a result", "[generator]") {
    const StyleProfile style = loadShipped("peak_time");
    std::atomic<bool> cancel{true};
    GenerationRequest request = requestFor(4, 11);
    request.cancel = &cancel;
    const SelectionResult result = generatePattern(style, request);
    CHECK_FALSE(result.success);
    CHECK(generateCandidate(style, request, 11).voices[0].archetypeId.empty());
}

TEST_CASE("a cancel flag that is not set changes nothing", "[generator]") {
    for (const auto& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        std::atomic<bool> cancel{false};
        GenerationRequest plain = requestFor(4, 5);
        GenerationRequest flagged = plain;
        flagged.cancel = &cancel;
        const SelectionResult a = generatePattern(style, plain);
        const SelectionResult b = generatePattern(style, flagged);
        REQUIRE(a.success);
        CHECK(b.success);
        CHECK(a.pattern == b.pattern);
        CHECK(a.seed == b.seed);
    }
}

// --- the chromatic share (SPEC 3.8) ---------------------------------------------------------------------------------

namespace {

/// How many notes of the voice are not in the scale of the pattern.
size_t offScale(const Pattern& pattern, size_t voice) {
    const Scale* scale = findScale(pattern.context.scaleId);
    REQUIRE(scale != nullptr);
    size_t count = 0;
    for (const Note& note : pattern.voices[voice].notes) {
        const int relative = ((note.pitch - pattern.context.root) % 12 + 12) % 12;
        const bool inScale = std::find(scale->intervals.begin(), scale->intervals.end(), relative) != scale->intervals.end();
        count += inScale ? 0 : 1;
    }
    return count;
}

} // namespace

TEST_CASE("the style with a chromatic share of its own", "[generator][chromatic]") {
    const StyleProfile style = loadShipped("peak_time");
    CHECK(style.chromaticDefaultPercent == 10);
    CHECK(withChromatic(style, -1).chromaticDefaultPercent == 10); // none: the style's
    CHECK(withChromatic(style, 0).chromaticDefaultPercent == 0);
    CHECK(withChromatic(style, 55).chromaticDefaultPercent == 55);
    CHECK(withChromatic(style, 500).chromaticDefaultPercent == 100);
    CHECK(withChromatic(style, 55).id == style.id); // nothing else changes
}

TEST_CASE("the less chromatic share the fewer notes off the scale, 0 % the fewest", "[generator][chromatic][series]") {
    size_t none = 0;
    size_t styleShare = 0;
    size_t most = 0;
    for (const std::string& name : kStyles) {
        const StyleProfile style = loadShipped(name);
        for (uint64_t seed = 1; seed <= 12; ++seed) {
            for (const int percent : {0, -1, 100}) {
                GenerationRequest request = requestFor(4, seed);
                request.settings.chromaticPct = percent;
                const auto result = generatePattern(style, request);
                REQUIRE(result.success);
                size_t count = 0;
                for (size_t v = 0; v < result.pattern.voices.size(); ++v) {
                    count += offScale(result.pattern, v);
                }
                (percent == 0 ? none : percent < 0 ? styleShare : most) += count;
                CHECK(result.pattern.info.chromaticPct == (percent < 0 ? -1 : percent)); // the pattern remembers it
            }
        }
    }
    // 0 % keeps the notes that belong to the chords (a cluster or a colour tone is no chromatic note of the line, measured
    // 40 in these series), the style's share adds some (64), 100 % many (259)
    CHECK(none < styleShare);
    CHECK(styleShare < most);
    CHECK(most > 3 * none);
}

TEST_CASE("the constraint layer and the quality criteria work with the share of the pattern", "[generator][chromatic]") {
    const StyleProfile style = loadShipped("melodic_techno"); // 0 % by default
    GenerationRequest request = requestFor(2, 5);
    const auto plain = generatePattern(style, request);
    REQUIRE(plain.success);
    CHECK(constraintSettingsFor(plain.pattern, style).chromaticPercent == style.chromaticDefaultPercent);
    request.settings.chromaticPct = 60;
    const auto chosen = generatePattern(style, request);
    REQUIRE(chosen.success);
    CHECK(constraintSettingsFor(chosen.pattern, style).chromaticPercent == 60); // not the style's: a variation or an edit keeps it
    CHECK(qualityContextFor(chosen.pattern, style, request.settings).chromaticPercent == 60);
    CHECK(qualityContextFor(plain.pattern, style, request.settings).chromaticPercent == style.chromaticDefaultPercent);
}

TEST_CASE("a voice generated anew keeps the share the pattern was made with, a request can change it", "[generator][chromatic]") {
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request = requestFor(4, 9);
    request.settings.chromaticPct = 100;
    auto made = generatePattern(style, request);
    REQUIRE(made.success);
    size_t withShare = 0;
    size_t withoutShare = 0;
    for (uint64_t seed = 1; seed <= 30; ++seed) {
        Pattern a = made.pattern;
        Pattern b = made.pattern;
        GenerationRequest keep = requestFor(4, 9); // no share in the request: the pattern's counts
        GenerationRequest style0 = requestFor(4, 9);
        style0.settings.chromaticPct = 0;
        REQUIRE(regenerateVoice(a, 1, style, keep, seed));
        REQUIRE(regenerateVoice(b, 1, style, style0, seed));
        withShare += offScale(a, 1);
        withoutShare += offScale(b, 1);
    }
    CHECK(withShare > 2 * withoutShare);
}

TEST_CASE("a generation around locked voices keeps the chromatic share and remembers it", "[generator][chromatic]") {
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request = requestFor(4, 3);
    const auto first = generatePattern(style, request);
    REQUIRE(first.success);
    Pattern locked = first.pattern;
    REQUIRE(setVoiceLocked(locked, 0, true) == 1);
    size_t plainOff = 0;
    size_t chosenOff = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        GenerationRequest plain = requestFor(4, seed);
        GenerationRequest chosen = requestFor(4, seed);
        chosen.settings.chromaticPct = 100;
        const auto a = generatePatternAroundLocks(style, plain, locked);
        const auto b = generatePatternAroundLocks(style, chosen, locked);
        REQUIRE((a.success && b.success));
        CHECK(a.pattern.info.chromaticPct == -1);
        CHECK(b.pattern.info.chromaticPct == 100); // the pattern remembers it
        plainOff += offScale(a.pattern, 1);
        chosenOff += offScale(b.pattern, 1);
    }
    CHECK(chosenOff > plainOff);
}
