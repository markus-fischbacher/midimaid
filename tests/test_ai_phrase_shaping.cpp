#include "ai/AiGeneration.h"
#include "ai/AiSchema.h"
#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"
#include "ai/PhraseShaping.h"
#include "core/FormPlan.h"
#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/StyleProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

using namespace mm::ai;
using namespace mm::core;
using nlohmann::json;

namespace {

StyleProfile shipped() {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/peak_time.json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

Phrase phrase(uint32_t start, PhraseRole role) {
    Phrase entry;
    entry.startBar = start;
    entry.lengthBars = 4;
    entry.role = role;
    return entry;
}

/// A 16-bar pattern tiled from a 2-bar motif, with four phrases of the given roles.
Pattern tiled(const StyleProfile& style, const std::vector<PhraseRole>& roles, uint64_t seed = 5) {
    GenerationRequest request;
    request.lengthBars = 2;
    request.seed = seed;
    const auto motif = generatePattern(style, request);
    REQUIRE(motif.success);
    json answer = json::parse(patternToSchemaJson(motif.pattern, &style, false)->json);
    answer["voices"][0]["motif_bars"] = 2;
    answer["voices"][1]["motif_bars"] = 2;
    const auto parsed = parseAiResponse(answer.dump());
    REQUIRE(parsed.ok);
    BuildContext context;
    context.lengthBars = 16;
    context.style = &style;
    context.styleId = style.id;
    const auto built = buildPattern(parsed.draft, context);
    REQUIRE(built.ok);
    Pattern pattern = built.pattern;
    pattern.phrases.clear();
    for (size_t i = 0; i < roles.size(); ++i) {
        pattern.phrases.push_back(phrase(static_cast<uint32_t>(i) * 4, roles[i]));
    }
    REQUIRE(validatePattern(pattern).empty());
    return pattern;
}

std::vector<Note> notesOf(const Pattern& pattern, size_t voice, uint32_t fromBar, uint32_t bars) {
    std::vector<Note> result;
    for (const auto& note : pattern.voices[voice].notes) {
        if (note.startTick >= fromBar * kTicksPerBar && note.startTick < (fromBar + bars) * kTicksPerBar) {
            result.push_back(note);
        }
    }
    return result;
}

double meanVelocity(const std::vector<Note>& notes) {
    double sum = 0;
    for (const auto& note : notes) {
        sum += note.velocity;
    }
    return notes.empty() ? 0 : sum / static_cast<double>(notes.size());
}

void shape(Pattern& pattern, const StyleProfile& style) {
    ArchetypeSettings settings;
    shapePhrasesByRole(pattern, style, settings);
}

void checkIdsUnique(const Pattern& pattern) {
    std::set<uint32_t> ids;
    for (const auto& track : pattern.voices) {
        for (const auto& note : track.notes) {
            CHECK(ids.insert(note.id).second);
            CHECK(note.id < pattern.nextNoteId);
        }
    }
}

} // namespace

TEST_CASE("main phrases keep the motif exactly", "[ai][shaping]") {
    const auto style = shipped();
    Pattern pattern = tiled(style, {PhraseRole::Main, PhraseRole::Main, PhraseRole::Main, PhraseRole::Main});
    const Pattern before = pattern;
    shape(pattern, style);
    CHECK(pattern == before);
}

TEST_CASE("a variation phrase changes the motif and leaves the other phrases alone", "[ai][shaping]") {
    const auto style = shipped();
    Pattern pattern = tiled(style, {PhraseRole::Main, PhraseRole::Variation, PhraseRole::Main, PhraseRole::Main});
    const Pattern before = pattern;
    shape(pattern, style);
    for (size_t voice = 0; voice < 2; ++voice) {
        CHECK(notesOf(pattern, voice, 0, 4) == notesOf(before, voice, 0, 4));
        CHECK(notesOf(pattern, voice, 8, 8) == notesOf(before, voice, 8, 8));
    }
    CHECK((notesOf(pattern, 0, 4, 4) != notesOf(before, 0, 4, 4) ||
           notesOf(pattern, 1, 4, 4) != notesOf(before, 1, 4, 4)));
    checkIdsUnique(pattern);
    CHECK(validatePattern(pattern).empty());
}

TEST_CASE("the shaping is deterministic", "[ai][shaping]") {
    const auto style = shipped();
    const std::vector<PhraseRole> roles = {PhraseRole::Main, PhraseRole::Variation, PhraseRole::Answer,
                                           PhraseRole::Build};
    Pattern first = tiled(style, roles);
    Pattern second = tiled(style, roles);
    shape(first, style);
    shape(second, style);
    CHECK(first == second);
}

TEST_CASE("an answer phrase changes the melody only", "[ai][shaping]") {
    const auto style = shipped();
    Pattern pattern = tiled(style, {PhraseRole::Main, PhraseRole::Answer, PhraseRole::Main, PhraseRole::Main});
    const Pattern before = pattern;
    shape(pattern, style);
    CHECK(pattern.voices[0] == before.voices[0]); // the bass
    CHECK(notesOf(pattern, 1, 4, 4) != notesOf(before, 1, 4, 4));
    CHECK(notesOf(pattern, 1, 0, 4) == notesOf(before, 1, 0, 4));
    checkIdsUnique(pattern);
}

TEST_CASE("a build phrase rises in velocity and accents the offbeats of its last bar", "[ai][shaping]") {
    const auto style = shipped();
    Pattern pattern = tiled(style, {PhraseRole::Main, PhraseRole::Build, PhraseRole::Main, PhraseRole::Main});
    const Pattern before = pattern;
    shape(pattern, style);
    for (size_t voice = 0; voice < 2; ++voice) {
        const auto first = notesOf(pattern, voice, 4, 1);
        const auto last = notesOf(pattern, voice, 7, 1);
        const auto firstBefore = notesOf(before, voice, 4, 1);
        const auto lastBefore = notesOf(before, voice, 7, 1);
        CHECK(meanVelocity(last) - meanVelocity(lastBefore) > meanVelocity(first) - meanVelocity(firstBefore));
        CHECK(meanVelocity(last) > meanVelocity(lastBefore) + 10);
        // the notes are the same ones
        REQUIRE(notesOf(pattern, voice, 4, 4).size() == notesOf(before, voice, 4, 4).size());
        CHECK(notesOf(pattern, voice, 0, 4) == notesOf(before, voice, 0, 4));
        CHECK(notesOf(pattern, voice, 8, 8) == notesOf(before, voice, 8, 8));
        for (const auto& note : last) {
            if ((note.startTick % kTicksPerBar) / 240 % 4 == 2) {
                CHECK(note.accent);
            }
        }
    }
}

TEST_CASE("a breakdown phrase thins out and gets softer, but no bar goes empty", "[ai][shaping]") {
    const auto style = shipped();
    Pattern pattern = tiled(style, {PhraseRole::Main, PhraseRole::Breakdown, PhraseRole::Main, PhraseRole::Main});
    const Pattern before = pattern;
    shape(pattern, style);
    for (size_t voice = 0; voice < 2; ++voice) {
        const auto after = notesOf(pattern, voice, 4, 4);
        const auto source = notesOf(before, voice, 4, 4);
        CHECK(after.size() < source.size());
        CHECK(meanVelocity(after) < meanVelocity(source));
        for (uint32_t bar = 4; bar < 8; ++bar) {
            if (!notesOf(before, voice, bar, 1).empty()) {
                CHECK_FALSE(notesOf(pattern, voice, bar, 1).empty());
            }
        }
        CHECK(notesOf(pattern, voice, 0, 4) == notesOf(before, voice, 0, 4));
        CHECK(notesOf(pattern, voice, 8, 8) == notesOf(before, voice, 8, 8));
    }
    checkIdsUnique(pattern);
}

TEST_CASE("a generated 16-bar pattern from a motif carries the roles of its phrase plan", "[ai][shaping][generate]") {
    const auto style = shipped();
    const auto loaded = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1",
                                            {"peak_time", "melodic_techno", "hard_industrial"});
    GenerationRequest request;
    request.lengthBars = 2;
    request.seed = 5;
    const auto motif = generatePattern(style, request);
    REQUIRE(motif.success);
    json answer = json::parse(patternToSchemaJson(motif.pattern, &style, false)->json);
    answer["voices"][0]["motif_bars"] = 2;
    answer["voices"][1]["motif_bars"] = 2;
    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 4}, {"role", "main"}},
                                     {{"start_bar", 4}, {"bars", 4}, {"role", "variation"}},
                                     {{"start_bar", 8}, {"bars", 4}, {"role", "build"}},
                                     {{"start_bar", 12}, {"bars", 4}, {"role", "breakdown"}}});
    MockProvider provider;
    provider.enqueueText(answer.dump());
    AiGenerateInput input;
    input.style = &style;
    input.templates = &loaded.templates;
    input.prompt.lengthBars = 16;
    input.model = "m";
    const auto result = generateWithAi(provider, input, CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    const Pattern& pattern = result.pattern;
    REQUIRE(pattern.phrases.size() == 4);
    CHECK(validatePattern(pattern).empty());
    // the first phrase repeats the motif, the breakdown has fewer notes than the main phrase
    CHECK(notesOf(pattern, 0, 12, 4).size() < notesOf(pattern, 0, 0, 4).size());
    CHECK(notesOf(pattern, 1, 12, 4).size() < notesOf(pattern, 1, 0, 4).size());
    CHECK(meanVelocity(notesOf(pattern, 1, 11, 1)) > meanVelocity(notesOf(pattern, 1, 8, 1)));
}
