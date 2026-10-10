#include "ai/AiGeneration.h"
#include "ai/MockProvider.h"
#include "ai/PatternCompact.h"

#include "core/PatternGenerator.h"
#include "core/PatternValidation.h"
#include "core/StyleProfile.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <thread>

using namespace mm::ai;
using namespace mm::core;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

StyleProfile shipped(const std::string& name = "peak_time") {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
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

/// The answer an AI would give if it wrote exactly this pattern.
std::string answerFor(const Pattern& pattern, const StyleProfile& style) {
    return patternToSchemaJson(pattern, &style, false)->json;
}

AiGenerateInput inputFor(const StyleProfile& style) {
    AiGenerateInput input;
    input.style = &style;
    input.templates = &templates();
    input.prompt.lengthBars = 4;
    input.prompt.energyPct = 70;
    input.prompt.creativityPct = 30;
    input.prompt.request = "dark rolling bass";
    input.model = "mock-model";
    input.maxTokens = 3000;
    input.timeoutSeconds = 20;
    input.createdUnixMs = 1234;
    return input;
}

AiResult okWithTokens(const std::string& text, int in, int out) {
    AiResult result;
    result.status = AiStatus::Ok;
    result.text = text;
    result.inputTokens = in;
    result.outputTokens = out;
    return result;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("a good answer becomes a valid, rated pattern with the data of the request", "[ai][generate]") {
    const auto style = shipped();
    const auto text = answerFor(algorithmic(style, 5), style);
    MockProvider mock;
    mock.enqueue(okWithTokens(text, 1200, 800));
    const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK_FALSE(result.repaired);
    CHECK(result.requests == 1);
    CHECK(result.inputTokens == 1200);
    CHECK(result.outputTokens == 800);
    const Pattern& p = result.pattern;
    CHECK(validatePattern(p).empty());
    CHECK(p.info.source == "ai");
    CHECK(p.info.prompt == "dark rolling bass");
    CHECK(p.info.promptVersion == kPromptVersion);
    CHECK(p.info.providerId == "mock");
    CHECK(p.info.modelId == "mock-model");
    CHECK(p.info.rawResponse == text);
    CHECK(p.info.createdUnixMs == 1234);
    CHECK(p.info.energyPct == 70);
    CHECK(p.info.creativityPct == 30);
    CHECK(p.qualityScore == result.score);
    CHECK(result.score > 0);
    CHECK(p.lengthBars == 4);
    CHECK_FALSE(p.voices[0].notes.empty());
    CHECK_FALSE(p.voices[1].notes.empty());
}

TEST_CASE("the request to the provider carries model, limits, temperature, schema and both prompts", "[ai][generate]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText(answerFor(algorithmic(style, 5), style));
    auto input = inputFor(style);
    REQUIRE(generateWithAi(mock, input, CancellationToken()).outcome == AiOutcome::Success);
    REQUIRE(mock.requestCount() == 1);
    const auto request = mock.requests()[0];
    CHECK(request.model == "mock-model");
    CHECK(request.maxTokens == 3000);
    CHECK(request.timeoutSeconds == 20);
    CHECK(request.temperature == 0.3);
    CHECK(request.schemaJson == schemaV1Json());
    CHECK(contains(request.systemPrompt, "Style profile"));
    CHECK(contains(request.userPrompt, "<<<dark rolling bass>>>"));
    CHECK(contains(request.userPrompt, "Energy: 0.70"));
    // the temperature follows the creativity
    for (const int creativity : {0, 100, 250, -4}) {
        MockProvider other;
        other.enqueueText(answerFor(algorithmic(style, 5), style));
        input.prompt.creativityPct = creativity;
        generateWithAi(other, input, CancellationToken());
        CHECK(other.requests()[0].temperature == std::clamp(creativity, 0, 100) / 100.0);
    }
}

TEST_CASE("an answer in a code fence is read", "[ai][generate]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText("```json\n" + answerFor(algorithmic(style, 6), style) + "\n```");
    CHECK(generateWithAi(mock, inputFor(style), CancellationToken()).outcome == AiOutcome::Success);
}

TEST_CASE("the constraint layer runs on what the AI wrote", "[ai][generate]") {
    const auto style = shipped();
    // a bass with a note on every kick step of every bar (the kick plays on 0, 4, 8, 12)
    json answer = json::parse(answerFor(algorithmic(style, 5), style));
    json bass = json::array();
    for (int bar = 0; bar < 4; ++bar) {
        for (const int step : {0, 4, 8, 12, 2, 6, 10, 14}) {
            bass.push_back({{"step", bar * 16 + step}, {"degree", 1}, {"len", 1}, {"vel", 90}});
        }
    }
    answer["voices"][0]["notes"] = bass;
    MockProvider mock;
    mock.enqueueText(answer.dump());
    const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    for (const auto& note : result.pattern.voices[0].notes) {
        CHECK(note.startTick % 960 != 0); // none starts on a kick step any more
    }
    CHECK(validatePattern(result.pattern).empty());
}

TEST_CASE("an unusable answer gets one repair request that names the problem", "[ai][generate]") {
    const auto style = shipped();
    const auto good = answerFor(algorithmic(style, 5), style);
    for (const std::string& bad : {std::string("I am sorry, I cannot do that."), std::string("{\"schema_version\": 1}"),
                                  std::string("{}"), std::string("")}) {
        MockProvider mock;
        mock.enqueue(okWithTokens(bad, 100, 5));
        mock.enqueue(okWithTokens(good, 110, 700));
        const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
        INFO(bad);
        REQUIRE(result.outcome == AiOutcome::Success);
        CHECK(result.repaired);
        CHECK(result.requests == 2);
        CHECK(result.inputTokens == 210);
        CHECK(result.outputTokens == 705);
        const auto requests = mock.requests();
        REQUIRE(requests.size() == 2);
        CHECK(requests[1].systemPrompt == requests[0].systemPrompt);
        CHECK(contains(requests[1].userPrompt, requests[0].userPrompt)); // the same request, plus the repair note
        CHECK(contains(requests[1].userPrompt, "could not be used"));
        CHECK(contains(requests[1].userPrompt, "JSON object only"));
        CHECK_FALSE(contains(requests[0].userPrompt, "could not be used"));
        CHECK(result.pattern.info.rawResponse == good); // the answer that worked
    }
}

TEST_CASE("the repair request says what was wrong", "[ai][generate]") {
    const auto style = shipped();
    json broken = json::parse(answerFor(algorithmic(style, 5), style));
    broken["context"]["scale"] = "klingon";
    MockProvider mock;
    mock.enqueueText(broken.dump());
    mock.enqueueText(answerFor(algorithmic(style, 5), style));
    REQUIRE(generateWithAi(mock, inputFor(style), CancellationToken()).outcome == AiOutcome::Success);
    CHECK(contains(mock.requests()[1].userPrompt, "context.scale"));
    CHECK(contains(mock.requests()[1].userPrompt, "klingon"));
}

TEST_CASE("an answer that leaves a voice without notes is repaired, too", "[ai][generate]") {
    const auto style = shipped();
    json empty = json::parse(answerFor(algorithmic(style, 5), style));
    empty["voices"][1]["notes"] = json::array();
    MockProvider mock;
    mock.enqueueText(empty.dump());
    mock.enqueueText(answerFor(algorithmic(style, 5), style));
    const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.repaired);
    CHECK(contains(mock.requests()[1].userPrompt, "melody voice has no notes"));
}

TEST_CASE("two unusable answers end as invalid, with the reason", "[ai][generate]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText("nonsense");
    mock.enqueueText("{\"schema_version\": 1, \"context\": {}}");
    mock.enqueueText(answerFor(algorithmic(style, 5), style)); // would be fine, but there is only one repair
    const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
    CHECK(result.outcome == AiOutcome::Invalid);
    CHECK(result.requests == 2);
    CHECK(mock.requestCount() == 2);
    CHECK(contains(result.error, "context.root")); // the problem of the second answer
}

TEST_CASE("a provider error is handed on at once, without a repair request", "[ai][generate]") {
    const auto style = shipped();
    for (const auto status : {AiStatus::AuthFailed, AiStatus::ModelNotFound, AiStatus::RateLimited,
                              AiStatus::ServerError, AiStatus::BadRequest, AiStatus::NetworkError, AiStatus::Timeout}) {
        MockProvider mock;
        mock.enqueueError(status, "the cause", 418);
        mock.enqueueText(answerFor(algorithmic(style, 5), style));
        const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
        CHECK(result.outcome == AiOutcome::ProviderError);
        CHECK(result.status == status);
        CHECK(result.httpStatus == 418);
        CHECK(result.error == "the cause");
        CHECK(result.requests == 1);
        CHECK(mock.requestCount() == 1);
    }
}

TEST_CASE("a provider error after an unusable answer is still a provider error", "[ai][generate]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText("nonsense");
    mock.enqueueError(AiStatus::ServerError, "down", 503);
    const auto result = generateWithAi(mock, inputFor(style), CancellationToken());
    CHECK(result.outcome == AiOutcome::ProviderError);
    CHECK(result.status == AiStatus::ServerError);
    CHECK(result.requests == 2);
}

TEST_CASE("a cancelled token ends the generation without a result", "[ai][generate][cancel]") {
    const auto style = shipped();
    {
        MockProvider mock;
        mock.enqueueText(answerFor(algorithmic(style, 5), style));
        CancellationToken token;
        token.cancel();
        const auto result = generateWithAi(mock, inputFor(style), token);
        CHECK(result.outcome == AiOutcome::Cancelled);
        CHECK(result.requests == 0);
        CHECK(mock.requestCount() == 0);
    }
    {
        MockProvider mock;
        mock.enqueueText(answerFor(algorithmic(style, 5), style), 10s);
        CancellationToken token;
        std::thread canceller([token] {
            std::this_thread::sleep_for(40ms);
            token.cancel();
        });
        const auto before = std::chrono::steady_clock::now();
        const auto result = generateWithAi(mock, inputFor(style), token);
        canceller.join();
        CHECK(result.outcome == AiOutcome::Cancelled);
        CHECK(std::chrono::steady_clock::now() - before < 3s);
    }
    {
        // the answer arrives, but the token was cancelled meanwhile: it is discarded
        MockProvider mock;
        CancellationToken token;
        AiResult answer = okWithTokens(answerFor(algorithmic(style, 5), style), 1, 1);
        mock.enqueue(answer);
        token.cancel();
        CHECK(generateWithAi(mock, inputFor(style), token).outcome == AiOutcome::Cancelled);
    }
}

namespace {

/// Answers fine, but the musician pressed cancel while it was working.
class CancelDuringCall : public IAiProvider {
public:
    explicit CancelDuringCall(std::string text) : text_(std::move(text)) {}
    ProviderInfo info() const override { return {"late", "Late", SchemaSupport::Enforced, {}}; }
    AiResult generate(const AiRequest&, const CancellationToken& token) override {
        token.cancel();
        AiResult result;
        result.status = AiStatus::Ok;
        result.text = text_;
        return result;
    }
    ConnectionStatus testConnection(const CancellationToken&) override { return {}; }

private:
    std::string text_;
};

} // namespace

TEST_CASE("an answer that arrives after the cancel is discarded", "[ai][generate][cancel]") {
    const auto style = shipped();
    CancelDuringCall provider(answerFor(algorithmic(style, 5), style));
    const auto result = generateWithAi(provider, inputFor(style), CancellationToken());
    CHECK(result.outcome == AiOutcome::Cancelled);
}

TEST_CASE("a cancel between the first and the repair request stops there", "[ai][generate][cancel]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText("nonsense", 300ms);
    mock.enqueueText(answerFor(algorithmic(style, 5), style));
    CancellationToken token;
    std::thread canceller([token] {
        std::this_thread::sleep_for(100ms);
        token.cancel();
    });
    const auto result = generateWithAi(mock, inputFor(style), token);
    canceller.join();
    CHECK(result.outcome == AiOutcome::Cancelled);
    CHECK(mock.requestCount() == 1); // the first request was made, never the repair
}

TEST_CASE("a score under the minimum of the style is reported, the pattern is still delivered", "[ai][generate]") {
    auto style = shipped();
    const auto text = answerFor(algorithmic(style, 5), style);
    style.quality.minScore = 101;
    MockProvider low;
    low.enqueueText(text);
    auto result = generateWithAi(low, inputFor(style), CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.belowMinScore);
    style.quality.minScore = 0;
    MockProvider fine;
    fine.enqueueText(text);
    result = generateWithAi(fine, inputFor(style), CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK_FALSE(result.belowMinScore);
}

TEST_CASE("without a style or templates nothing is asked", "[ai][generate]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText("x");
    auto input = inputFor(style);
    input.style = nullptr;
    CHECK(generateWithAi(mock, input, CancellationToken()).outcome == AiOutcome::Invalid);
    input = inputFor(style);
    input.templates = nullptr;
    CHECK(generateWithAi(mock, input, CancellationToken()).outcome == AiOutcome::Invalid);
    CHECK(mock.requestCount() == 0);
}

TEST_CASE("locked voices stay, the answer is written for their harmony", "[ai][generate][lock]") {
    const auto style = shipped();
    Pattern current = algorithmic(style, 11);
    current.voices[0].lock = {true, true, true};
    const Track lockedBass = current.voices[0];
    // the AI writes a different pattern in another key: its melody is used, in the key of the locked bass
    Pattern other = algorithmic(style, 12);
    other.context.root = static_cast<PitchClass>((current.context.root + 5) % 12);
    json answer = json::parse(answerFor(other, style));
    answer["voices"][0]["notes"] = json::array(); // locked: returned empty
    answer["context"]["progression"] = json::array({"not a chord"}); // the harmony of the locked voice counts, not this
    current.kickGridId = "broken_a";
    current.voices[0].lock = {true, true, true};
    const Track lockedBassNow = current.voices[0];
    MockProvider mock;
    mock.enqueueText(answer.dump());
    auto input = inputFor(style);
    input.lockedFrom = current;
    input.prompt.lengthBars = 16; // the request says 16, the locked pattern says 4: the pattern wins
    input.prompt.root = 3;
    const auto result = generateWithAi(mock, input, CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    const Pattern& p = result.pattern;
    CHECK(p.voices[0] == lockedBass); // notes, ids, lock: exactly as they were
    CHECK(p.voices[0] == lockedBassNow);
    CHECK(p.context == current.context);
    CHECK(p.phrases == current.phrases);
    CHECK(p.kickGridId == current.kickGridId);
    CHECK(p.lengthBars == current.lengthBars);
    CHECK_FALSE(p.voices[1].notes.empty());
    std::set<uint32_t> ids;
    for (const auto& track : p.voices) {
        for (const auto& note : track.notes) {
            CHECK(ids.insert(note.id).second);
            CHECK(note.id < p.nextNoteId);
        }
    }
    CHECK(validatePattern(p).empty());
    // the request names the locked voice and the harmony it was made for, and fixes key and scale
    const auto user = mock.requests()[0].userPrompt;
    CHECK(contains(user, "locked"));
    CHECK(contains(user, "bass"));
    CHECK(contains(user, "Length: 4 bars"));
    CHECK(contains(user, "fixed, use " + rootName(current.context.root)));
    CHECK(contains(user, "fixed, use " + current.context.scaleId));
    const auto symbols = progressionSymbols(current);
    REQUIRE(symbols.has_value());
    std::string joined;
    for (const auto& symbol : *symbols) {
        joined += (joined.empty() ? "" : ", ") + symbol;
    }
    CHECK(contains(user, "Keep this progression (one entry per bar): " + joined + "."));
}

TEST_CASE("the phrases of the locked pattern stay", "[ai][generate][lock]") {
    const auto style = shipped();
    Pattern current = algorithmic(style, 21, 8);
    current.phrases.clear();
    Phrase first;
    first.startBar = 0;
    first.lengthBars = 4;
    first.role = PhraseRole::Main;
    Phrase second;
    second.startBar = 4;
    second.lengthBars = 4;
    second.role = PhraseRole::Answer;
    current.phrases = {first, second};
    current.voices[1].lock = {true, true, true};
    REQUIRE(validatePattern(current).empty());
    json answer = json::parse(answerFor(algorithmic(style, 22, 8), style));
    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 8}, {"role", "build"}}});
    answer["voices"][1]["notes"] = json::array();
    MockProvider mock;
    mock.enqueueText(answer.dump());
    auto input = inputFor(style);
    input.lockedFrom = current;
    const auto result = generateWithAi(mock, input, CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK(result.pattern.phrases == current.phrases);
    CHECK(result.pattern.voices[1] == current.voices[1]);
    CHECK_FALSE(result.pattern.voices[0].notes.empty());
}

TEST_CASE("an unlocked pattern gives a request without lock note", "[ai][generate][lock]") {
    const auto style = shipped();
    MockProvider mock;
    mock.enqueueText(answerFor(algorithmic(style, 5), style));
    Pattern current = algorithmic(style, 5);
    auto input = inputFor(style);
    input.lockedFrom = current; // nothing locked
    // (the caller only passes `lockedFrom` when a voice is locked; with none the request just keeps the harmony)
    const auto result = generateWithAi(mock, input, CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    CHECK_FALSE(contains(mock.requests()[0].userPrompt, "are locked"));
}

TEST_CASE("a 16-bar answer is a motif that is repeated over the pattern", "[ai][generate][motif]") {
    const auto style = shipped();
    json answer = json::parse(answerFor(algorithmic(style, 5, 2), style)); // 2 bars: the motif
    answer["voices"][0]["motif_bars"] = 2;
    answer["voices"][1]["motif_bars"] = 2;
    answer["phrases"] = json::array({{{"start_bar", 0}, {"bars", 8}, {"role", "main"}},
                                     {{"start_bar", 8}, {"bars", 8}, {"role", "variation"}}});
    MockProvider mock;
    mock.enqueueText(answer.dump());
    auto input = inputFor(style);
    input.prompt.lengthBars = 16;
    const auto result = generateWithAi(mock, input, CancellationToken());
    REQUIRE(result.outcome == AiOutcome::Success);
    const Pattern& p = result.pattern;
    CHECK(p.lengthBars == 16);
    CHECK(p.phrases.size() == 2);
    CHECK(validatePattern(p).empty());
    CHECK(contains(mock.requests()[0].userPrompt, "motif_bars"));
    // the last bars carry the motif, too
    const auto last = std::count_if(p.voices[1].notes.begin(), p.voices[1].notes.end(),
                                    [](const Note& n) { return n.startTick >= 14 * kTicksPerBar; });
    CHECK(last > 0);
}
