#include "ai/AiRefine.h"
#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"
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

const PromptTemplates& templates() {
    static const PromptTemplates loaded = [] {
        const auto result = loadPromptTemplates(std::string(MIDIMAID_RESOURCE_DIR) + "/prompts/v1",
                                                {"peak_time", "melodic_techno", "hard_industrial"});
        REQUIRE(result.missing.empty());
        return result.templates;
    }();
    return loaded;
}

Pattern algorithmic(const StyleProfile& style, uint64_t seed, uint32_t bars = 4) {
    GenerationRequest request;
    request.lengthBars = bars;
    request.seed = seed;
    const auto result = generatePattern(style, request);
    REQUIRE(result.success);
    return result.pattern;
}

/// The answer of an AI that writes exactly the pattern it was given, as json to edit.
json answerOf(const Pattern& pattern, const StyleProfile& style) {
    return json::parse(patternToSchemaJson(pattern, &style, true)->json);
}

AiRefineInput inputFor(const StyleProfile& style, const Pattern& pattern,
                       const std::string& instruction = "fewer notes") {
    AiRefineInput input;
    input.style = &style;
    input.templates = &templates();
    input.pattern = pattern;
    input.instruction = instruction;
    input.model = "mock-model";
    input.createdUnixMs = 777;
    return input;
}

AiGenerateResult refine(MockProvider& provider, const AiRefineInput& input) {
    return refineWithAi(provider, input, CancellationToken());
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

Phrase phraseOf(uint32_t start, PhraseRole role) {
    Phrase phrase;
    phrase.startBar = start;
    phrase.lengthBars = 4;
    phrase.role = role;
    return phrase;
}

/// A pattern whose second bass note is off the 16th grid, like a played or imported one.
Pattern withOffGridNote(const StyleProfile& style, uint32_t bars = 4) {
    Pattern pattern = algorithmic(style, 21, bars);
    auto& notes = pattern.voices[0].notes;
    REQUIRE(notes.size() > 3);
    notes[1].startTick += 5;
    notes[1].lengthTicks = notes[1].lengthTicks > 20 ? notes[1].lengthTicks - 5 : notes[1].lengthTicks;
    return pattern;
}

std::vector<uint32_t> idsOf(const Track& track) {
    std::vector<uint32_t> ids;
    for (const auto& note : track.notes) {
        ids.push_back(note.id);
    }
    return ids;
}

} // namespace

TEST_CASE("an answer that returns the notes unchanged keeps every note exactly, off-grid ticks included",
          "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = withOffGridNote(style);
    MockProvider provider;
    provider.enqueueText(answerOf(original, style).dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    REQUIRE(result.pattern.voices.size() == original.voices.size());
    for (size_t v = 0; v < original.voices.size(); ++v) {
        REQUIRE(result.pattern.voices[v].notes.size() == original.voices[v].notes.size());
        for (size_t n = 0; n < original.voices[v].notes.size(); ++n) {
            const auto& before = original.voices[v].notes[n];
            const auto& after = result.pattern.voices[v].notes[n];
            CHECK(after.id == before.id);
            CHECK(after.startTick == before.startTick);
            CHECK(after.lengthTicks == before.lengthTicks);
            CHECK(after.lock == before.lock); // the lock that held the note during the constraint layer is gone
        }
    }
    CHECK(result.pattern.voices[0].notes[1].startTick % 240 == 5);
}

TEST_CASE("a refinement is marked as such, keeps the context and records the instruction", "[ai][refine]") {
    const auto style = shipped();
    Pattern original = algorithmic(style, 3);
    original.info.prompt = "the first wish";
    MockProvider provider;
    const std::string text = answerOf(original, style).dump();
    provider.enqueueText(text);
    const auto result = refine(provider, inputFor(style, original, "more slides"));
    REQUIRE(result.outcome == AiOutcome::Success);
    const Pattern& pattern = result.pattern;
    CHECK(pattern.info.source == "refine");
    CHECK(pattern.info.providerId == "mock");
    CHECK(pattern.info.modelId == "mock-model");
    CHECK(pattern.info.rawResponse == text);
    CHECK(pattern.info.createdUnixMs == 777);
    CHECK(pattern.info.prompt == "the first wish"); // the wish of the generation stays; refinements are in the history
    CHECK(pattern.refineHistory == std::vector<std::string>{"more slides"});
    CHECK(pattern.context == original.context);
    CHECK(pattern.phrases == original.phrases);
    CHECK(pattern.kickGridId == original.kickGridId);
    CHECK(pattern.styleId == original.styleId);
    CHECK(pattern.qualityScore == result.score);
    CHECK(validatePattern(pattern).empty());
}

TEST_CASE("a changed note takes the answer's values and keeps its id; new notes get new ids", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 4);
    auto answer = answerOf(original, style);
    auto& notes = answer["voices"][0]["notes"];
    const uint32_t changedId = notes[2]["id"];
    const uint32_t removedId = notes[3]["id"];
    notes[2]["vel"] = notes[2]["vel"].get<int>() > 60 ? 50 : 120;
    notes.erase(notes.begin() + 3);
    std::set<int> taken;
    for (const auto& note : notes) {
        taken.insert(note["step"].get<int>());
    }
    int freeStep = 1;
    while (taken.count(freeStep) != 0) {
        ++freeStep;
    }
    notes.push_back({{"step", freeStep}, {"degree", 1}, {"octave", 0}, {"len", 1}, {"vel", 99}});
    MockProvider provider;
    provider.enqueueText(answer.dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    const auto& bass = result.pattern.voices[0];
    const auto ids = idsOf(bass);
    CHECK(std::find(ids.begin(), ids.end(), changedId) != ids.end());
    CHECK(std::find(ids.begin(), ids.end(), removedId) == ids.end());
    const auto changed =
        std::find_if(bass.notes.begin(), bass.notes.end(), [&](const Note& n) { return n.id == changedId; });
    REQUIRE(changed != bass.notes.end());
    CHECK((changed->velocity == 50 || changed->velocity == 120));
    // exactly one id is new, and above every id the pattern had
    size_t fresh = 0;
    for (const auto id : ids) {
        if (id >= original.nextNoteId) {
            ++fresh;
        }
    }
    CHECK(fresh == 1);
    CHECK(result.pattern.nextNoteId > original.nextNoteId);
    std::set<uint32_t> unique(ids.begin(), ids.end());
    CHECK(unique.size() == ids.size());
}

TEST_CASE("every field the schema carries counts as a change of a note", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 22);
    const auto findNote = [](const Pattern& pattern, uint32_t id) {
        const auto& notes = pattern.voices[1].notes;
        const auto found = std::find_if(notes.begin(), notes.end(), [&](const Note& n) { return n.id == id; });
        REQUIRE(found != notes.end());
        return *found;
    };
    // the note that the answer does not change, as the pipeline returns it: the baseline for each field
    auto same = answerOf(original, style);
    const uint32_t id = same["voices"][1]["notes"][1]["id"];
    MockProvider baseProvider;
    baseProvider.enqueueText(same.dump());
    auto input = inputFor(style, original);
    input.voice = 1;
    const auto baseResult = refine(baseProvider, input);
    REQUIRE(baseResult.outcome == AiOutcome::Success);
    const Note base = findNote(baseResult.pattern, id);

    const std::vector<std::string> fields = {"step", "len", "degree", "vel", "accent", "slide"};
    for (const auto& field : fields) {
        auto answer = answerOf(original, style);
        auto& note = answer["voices"][1]["notes"][1];
        if (field == "step") {
            note["step"] = note["step"].get<int>() + 1;
        } else if (field == "len") {
            note["len"] = note["len"].get<int>() + 1;
        } else if (field == "degree") {
            note["degree"] = note["degree"].get<int>() % 7 + 1;
        } else if (field == "vel") {
            note["vel"] = note["vel"].get<int>() + 5;
        } else {
            note[field] = !note.value(field, false);
        }
        MockProvider provider;
        provider.enqueueText(answer.dump());
        const auto result = refine(provider, input);
        INFO(field);
        REQUIRE(result.outcome == AiOutcome::Success);
        const Note changed = findNote(result.pattern, id);
        if (field == "step") {
            CHECK(changed.startTick != base.startTick);
        } else if (field == "len") {
            CHECK(changed.lengthTicks != base.lengthTicks);
        } else if (field == "degree") {
            CHECK(changed.pitch != base.pitch);
        } else if (field == "vel") {
            CHECK(changed.velocity != base.velocity);
        } else if (field == "accent") {
            CHECK(changed.accent != base.accent);
        } else {
            CHECK(changed.slide != base.slide);
        }
    }
}

TEST_CASE("unknown and repeated ids make new notes, never two notes with one id", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 6);
    auto answer = answerOf(original, style);
    auto& notes = answer["voices"][0]["notes"];
    REQUIRE(notes.size() > 3);
    const uint32_t firstId = notes[0]["id"];
    notes[1]["id"] = 424242;  // unknown
    notes[2]["id"] = firstId; // repeated
    MockProvider provider;
    provider.enqueueText(answer.dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    std::set<uint32_t> all;
    size_t total = 0;
    for (const auto& track : result.pattern.voices) {
        for (const auto& note : track.notes) {
            all.insert(note.id);
            ++total;
            CHECK(note.id != 424242);
        }
    }
    CHECK(all.size() == total);
}

TEST_CASE("refining one voice leaves the other voice exactly as it was", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 8);
    auto answer = answerOf(original, style);
    for (auto& voice : answer["voices"]) { // the answer changes both voices
        for (auto& note : voice["notes"]) {
            note["vel"] = 45;
        }
    }
    MockProvider provider;
    provider.enqueueText(answer.dump());
    auto input = inputFor(style, original);
    input.voice = 1;
    const auto result = refine(provider, input);
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.pattern.voices[0] == original.voices[0]);
    CHECK(result.pattern.voices[1].notes != original.voices[1].notes);
    CHECK(contains(provider.requests().front().userPrompt, "only the melody voice"));
}

TEST_CASE("refining one voice does not need the other voice in the answer", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 9);
    auto answer = answerOf(original, style);
    answer["voices"].erase(answer["voices"].begin()); // only the melody
    answer["voices"][0]["notes"][0]["vel"] = 33;
    MockProvider provider;
    provider.enqueueText(answer.dump());
    auto input = inputFor(style, original);
    input.voice = 1;
    const auto result = refine(provider, input);
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.requests == 1);
    CHECK(result.pattern.voices[0] == original.voices[0]);
}

TEST_CASE("a locked voice comes back unchanged, also when the answer changes or drops it", "[ai][refine]") {
    const auto style = shipped();
    Pattern original = algorithmic(style, 10);
    original.voices[0].lock = {true, true, true};
    auto answer = answerOf(original, style);
    answer["voices"][0]["notes"] = json::array(); // the locked bass is returned empty
    for (auto& note : answer["voices"][1]["notes"]) {
        note["vel"] = 60;
    }
    MockProvider provider;
    provider.enqueueText(answer.dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.pattern.voices[0] == original.voices[0]);
    CHECK(contains(provider.requests().front().userPrompt, "Locked, return these voices unchanged: bass"));
}

TEST_CASE("refining a phrase changes only its bars", "[ai][refine]") {
    const auto style = shipped();
    Pattern original = algorithmic(style, 11, 8);
    original.phrases.clear();
    original.phrases.push_back(phraseOf(0, PhraseRole::Main));
    original.phrases.push_back(phraseOf(4, PhraseRole::Variation));
    REQUIRE(validatePattern(original).empty());
    auto answer = answerOf(original, style);
    for (auto& voice : answer["voices"]) { // the answer changes every note, in both phrases
        for (auto& note : voice["notes"]) {
            note["vel"] = 41;
        }
    }
    MockProvider provider;
    provider.enqueueText(answer.dump());
    auto input = inputFor(style, original);
    input.phrase = 1;
    const auto result = refine(provider, input);
    REQUIRE(result.outcome == AiOutcome::Success);
    for (size_t v = 0; v < original.voices.size(); ++v) {
        std::vector<Note> firstBefore;
        std::vector<Note> firstAfter;
        size_t changedInSecond = 0;
        for (const auto& note : original.voices[v].notes) {
            if (note.startTick < 4 * kTicksPerBar) {
                firstBefore.push_back(note);
            }
        }
        for (const auto& note : result.pattern.voices[v].notes) {
            if (note.startTick < 4 * kTicksPerBar) {
                firstAfter.push_back(note);
            } else if (note.velocity == 41) {
                ++changedInSecond;
            }
        }
        CHECK(firstAfter == firstBefore);
        CHECK(changedInSecond > 0);
    }
    CHECK(contains(provider.requests().front().userPrompt, "only the bars 4 to 7"));
}

TEST_CASE("a voice wins over a phrase: the whole voice is refined", "[ai][refine]") {
    const auto style = shipped();
    Pattern original = algorithmic(style, 23, 8);
    original.phrases.clear();
    original.phrases.push_back(phraseOf(0, PhraseRole::Main));
    original.phrases.push_back(phraseOf(4, PhraseRole::Variation));
    auto answer = answerOf(original, style);
    for (auto& note : answer["voices"][1]["notes"]) {
        note["vel"] = 41;
    }
    MockProvider provider;
    provider.enqueueText(answer.dump());
    auto input = inputFor(style, original);
    input.voice = 1;
    input.phrase = 1;
    const auto result = refine(provider, input);
    REQUIRE(result.outcome == AiOutcome::Success);
    size_t changedInFirstPhrase = 0;
    for (const auto& note : result.pattern.voices[1].notes) {
        if (note.startTick < 4 * kTicksPerBar && note.velocity == 41) {
            ++changedInFirstPhrase;
        }
    }
    CHECK(changedInFirstPhrase > 0);
    CHECK(contains(provider.requests().front().userPrompt, "only the melody voice"));
}

TEST_CASE("an answer id that belongs to a note outside the phrase does not take that id twice", "[ai][refine]") {
    const auto style = shipped();
    Pattern original = algorithmic(style, 12, 8);
    original.phrases.clear();
    original.phrases.push_back(phraseOf(0, PhraseRole::Main));
    original.phrases.push_back(phraseOf(4, PhraseRole::Variation));
    auto answer = answerOf(original, style);
    // a note of the second phrase carries the id of a note in the first
    uint32_t firstPhraseId = 0;
    for (const auto& note : answer["voices"][0]["notes"]) {
        if (note["step"].get<int>() < 64) {
            firstPhraseId = note["id"];
            break;
        }
    }
    for (auto& note : answer["voices"][0]["notes"]) {
        if (note["step"].get<int>() >= 64) {
            note["id"] = firstPhraseId;
            break;
        }
    }
    MockProvider provider;
    provider.enqueueText(answer.dump());
    auto input = inputFor(style, original);
    input.phrase = 1;
    const auto result = refine(provider, input);
    REQUIRE(result.outcome == AiOutcome::Success);
    const auto ids = idsOf(result.pattern.voices[0]);
    std::set<uint32_t> unique(ids.begin(), ids.end());
    CHECK(unique.size() == ids.size());
}

TEST_CASE("key, scale and progression stay, whatever the answer says", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 13);
    auto answer = answerOf(original, style);
    answer["context"]["root"] = original.context.root == 0 ? "D" : "C";
    answer["context"]["scale"] = "dorian";
    answer["context"]["progression"] = json::array({"Em"});
    MockProvider provider;
    provider.enqueueText(answer.dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.pattern.context == original.context);
    // the degrees of the answer are read in the key of the pattern, not in the key the answer names
    for (size_t v = 0; v < original.voices.size(); ++v) {
        REQUIRE(result.pattern.voices[v].notes.size() == original.voices[v].notes.size());
        for (size_t n = 0; n < original.voices[v].notes.size(); ++n) {
            CHECK(result.pattern.voices[v].notes[n].pitch == original.voices[v].notes[n].pitch);
        }
    }
}

TEST_CASE("the history keeps the last five instructions and the prompt sends them", "[ai][refine]") {
    const auto style = shipped();
    Pattern current = algorithmic(style, 14);
    MockProvider provider;
    for (int i = 1; i <= 7; ++i) {
        provider.enqueueText(answerOf(current, style).dump());
        const auto result = refine(provider, inputFor(style, current, "wish number " + std::to_string(i)));
        REQUIRE(result.outcome == AiOutcome::Success);
        current = result.pattern;
        CHECK(current.refineHistory.size() == std::min<size_t>(static_cast<size_t>(i), kRefineHistorySize));
        const auto prompt = provider.requests().back().userPrompt;
        if (i == 1) {
            CHECK(!contains(prompt, "Earlier refinements"));
        } else {
            CHECK(contains(prompt, "wish number " + std::to_string(i - 1)));
        }
        if (i == 7) {
            CHECK(!contains(prompt, "wish number 1>>>")); // six were done, only the last five are sent
            CHECK(contains(prompt, "wish number 2>>>"));
            CHECK(contains(prompt, "wish number 6>>>"));
        }
    }
    CHECK(current.refineHistory.size() == kRefineHistorySize);
    CHECK(current.refineHistory.front() == "wish number 3");
    CHECK(current.refineHistory.back() == "wish number 7");
}

TEST_CASE("an unusable answer gets one repair request, a provider error none", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 15);
    {
        MockProvider provider;
        provider.enqueueText("sorry, I cannot do that");
        provider.enqueueText(answerOf(original, style).dump());
        const auto result = refine(provider, inputFor(style, original));
        REQUIRE(result.outcome == AiOutcome::Success);
        CHECK(result.repaired);
        CHECK(result.requests == 2);
        CHECK(contains(provider.requests()[1].userPrompt, "could not be used"));
    }
    {
        MockProvider provider;
        provider.enqueueText("nope");
        provider.enqueueText("still nope");
        const auto result = refine(provider, inputFor(style, original));
        CHECK(result.outcome == AiOutcome::Invalid);
        CHECK(result.requests == 2);
    }
    {
        MockProvider provider;
        provider.enqueueError(AiStatus::AuthFailed, "bad key", 401);
        const auto result = refine(provider, inputFor(style, original));
        CHECK(result.outcome == AiOutcome::ProviderError);
        CHECK(result.status == AiStatus::AuthFailed);
        CHECK(result.requests == 1);
    }
}

TEST_CASE("an answer that empties the voice that is refined is repaired", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 16);
    auto empty = answerOf(original, style);
    empty["voices"][0]["notes"] = json::array();
    MockProvider provider;
    provider.enqueueText(empty.dump());
    provider.enqueueText(answerOf(original, style).dump());
    auto input = inputFor(style, original);
    input.voice = 0;
    const auto result = refine(provider, input);
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.repaired);
}

TEST_CASE("a cancelled token ends a refinement without a result", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 17);
    MockProvider provider;
    provider.enqueueText(answerOf(original, style).dump());
    CancellationToken token;
    token.cancel();
    const auto result = refineWithAi(provider, inputFor(style, original), token);
    CHECK(result.outcome == AiOutcome::Cancelled);
    CHECK(provider.requestCount() == 0);
}

TEST_CASE("the prompt log carries what was sent, and a missing style is refused", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 18);
    MockProvider provider;
    provider.enqueueText(answerOf(original, style).dump());
    const auto result = refine(provider, inputFor(style, original, "less busy"));
    CHECK(contains(result.userPrompt, "less busy"));
    CHECK(!result.systemPrompt.empty());
    CHECK(!result.lastAnswer.empty());

    AiRefineInput bad = inputFor(style, original);
    bad.style = nullptr;
    CHECK(refine(provider, bad).outcome == AiOutcome::Invalid);
}

TEST_CASE("a 16 bar pattern is refined with its ids", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 19, 16);
    MockProvider provider;
    provider.enqueueText(answerOf(original, style).dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(idsOf(result.pattern.voices[0]) == idsOf(original.voices[0]));
    CHECK(result.pattern.lengthBars == 16);
}

TEST_CASE("the refinement is rated and the constraint layer runs on it", "[ai][refine]") {
    const auto style = shipped();
    const Pattern original = algorithmic(style, 20);
    auto answer = answerOf(original, style);
    for (auto& note : answer["voices"][0]["notes"]) { // far out of the bass range
        note["octave"] = 4;
    }
    MockProvider provider;
    provider.enqueueText(answer.dump());
    const auto result = refine(provider, inputFor(style, original));
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.score > 0);
    CHECK(validatePattern(result.pattern).empty());
    for (const auto& note : result.pattern.voices[0].notes) {
        CHECK(note.pitch < 72); // the constraint layer pulled the notes back into the bass register
    }
}

TEST_CASE("the merge gives a note that takes the id of a note outside the phrase a new id", "[ai][refine]") {
    const auto style = shipped();
    Pattern current = algorithmic(style, 24, 8);
    current.phrases.clear();
    current.phrases.push_back(phraseOf(0, PhraseRole::Main));
    current.phrases.push_back(phraseOf(4, PhraseRole::Variation));
    Pattern answer = current; // the answer writes the same notes ...
    auto& notes = answer.voices[0].notes;
    uint32_t outsideId = 0;
    for (const auto& note : notes) {
        if (note.startTick < 4 * kTicksPerBar) {
            outsideId = note.id;
            break;
        }
    }
    for (auto& note : notes) { // ... but a note of the second phrase carries the id of one in the first
        if (note.startTick >= 4 * kTicksPerBar) {
            note.id = outsideId;
            break;
        }
    }
    const Pattern merged = mergeRefinement(current, current, answer, std::nullopt, 1);
    std::set<uint32_t> ids;
    for (const auto& note : merged.voices[0].notes) {
        CHECK(ids.insert(note.id).second);
    }
    CHECK(merged.nextNoteId > current.nextNoteId);
}
