#include "ai/ModelsConfig.h"
#include "ai/OpenAiProvider.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

using namespace mm::ai;

namespace {

std::string shippedText() {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/config/models.json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

class NullClient : public IHttpClient {
public:
    HttpResponse send(const HttpRequest&, const CancellationToken&) override { return {}; }
};

} // namespace

TEST_CASE("the shipped models.json has the providers of the specification", "[ai-models]") {
    const auto config = parseModelsConfig(shippedText());
    for (const char* id : {"anthropic", "openai", "ollama", "lmstudio", "openrouter", "custom"}) {
        INFO(id);
        CHECK(config.find(id) != nullptr);
    }
    CHECK(config.providers.size() == 6);
}

TEST_CASE("Anthropic recommends Sonnet, the others take their list from the provider", "[ai-models]") {
    const auto config = parseModelsConfig(shippedText());
    const auto* anthropic = config.find("anthropic");
    REQUIRE(anthropic != nullptr);
    CHECK(anthropic->recommendedModel == "claude-sonnet-5-5");
    CHECK(anthropic->kind == ProviderKind::Anthropic);
    CHECK(anthropic->schemaSupport == SchemaSupport::Enforced);
    for (const char* id : {"openai", "ollama", "lmstudio", "openrouter", "custom"}) {
        INFO(id);
        CHECK(config.find(id)->recommendedModel.empty());
    }
}

TEST_CASE("the shipped file agrees with the presets in the code", "[ai-models]") {
    const auto config = parseModelsConfig(shippedText());
    const std::pair<const char*, OpenAiPreset> pairs[] = {{"openai", OpenAiPreset::OpenAi},
                                                          {"ollama", OpenAiPreset::Ollama},
                                                          {"lmstudio", OpenAiPreset::LmStudio},
                                                          {"openrouter", OpenAiPreset::OpenRouter}};
    for (const auto& [id, preset] : pairs) {
        INFO(id);
        const auto code = openAiPreset(preset);
        const auto* entry = config.find(id);
        REQUIRE(entry != nullptr);
        CHECK(entry->baseUrl == code.baseUrl);
        CHECK(entry->schemaSupport == code.schemaSupport);
        CHECK(entry->maxCompletionTokens == code.useMaxCompletionTokens);
        CHECK(entry->filterChatModels == code.filterChatModels);
    }
    CHECK(config.find("custom")->schemaSupport == SchemaSupport::JsonOnly); // SPEC 7.2: at most "json only"
}

TEST_CASE("cloud and local providers, keys and timeouts", "[ai-models]") {
    const auto config = parseModelsConfig(shippedText());
    CHECK(config.find("anthropic")->cloud);
    CHECK(config.find("openai")->cloud);
    CHECK_FALSE(config.find("ollama")->cloud);
    CHECK_FALSE(config.find("lmstudio")->cloud);
    CHECK(config.find("openai")->keyRequired);
    CHECK_FALSE(config.find("ollama")->keyRequired);
    CHECK(config.find("anthropic")->timeoutSeconds == 30);
    CHECK(config.find("ollama")->timeoutSeconds == 600); // SPEC 7.5, D-174: thinking models need minutes
}

TEST_CASE("an address on this computer is never cloud, whatever the file says", "[ai-models]") {
    const auto config = parseModelsConfig(R"({"providers":[
        {"id":"a","kind":"openai","baseUrl":"http://127.0.0.1:1/v1","cloud":true},
        {"id":"b","kind":"openai","baseUrl":"https://example.com/v1","cloud":false},
        {"id":"c","kind":"openai","baseUrl":"http://localhost.evil.com/v1","cloud":true}]})");
    CHECK_FALSE(config.find("a")->cloud);
    CHECK_FALSE(config.find("b")->cloud); // the file may declare a provider local; it is only forced the other way
    CHECK(config.find("c")->cloud);
}

TEST_CASE("bad entries are left out and bad fields get defaults", "[ai-models]") {
    const auto config = parseModelsConfig(R"({"providers":[
        {"kind":"openai"}, {"id":"x","kind":"magic"}, 5, {"id":"y","kind":"openai","timeoutSeconds":99999,
         "schemaSupport":"wow","models":["m",3,""],"name":7},
        {"id":"y","kind":"openai"}]})");
    REQUIRE(config.providers.size() == 1); // the duplicate is dropped too
    const auto& y = config.providers[0];
    CHECK(y.id == "y");
    CHECK(y.name == "y");
    CHECK(y.timeoutSeconds == 30);
    CHECK(y.schemaSupport == SchemaSupport::JsonOnly);
    CHECK(y.models == std::vector<std::string>{"m"});
}

TEST_CASE("unreadable text gives an empty list", "[ai-models]") {
    CHECK(parseModelsConfig("").providers.empty());
    CHECK(parseModelsConfig("nope").providers.empty());
    CHECK(parseModelsConfig("[]").providers.empty());
    CHECK(parseModelsConfig(R"({"providers":5})").providers.empty());
}

TEST_CASE("the file of the user replaces entries by id and adds new ones", "[ai-models]") {
    const auto shipped = parseModelsConfig(shippedText());
    const auto user = parseModelsConfig(R"({"providers":[
        {"id":"anthropic","kind":"anthropic","baseUrl":"https://api.anthropic.com","recommendedModel":"claude-haiku-5-5"},
        {"id":"mine","kind":"openai","baseUrl":"https://llm.example.com/v1"}]})");
    const auto merged = mergeModelsConfig(shipped, user);
    CHECK(merged.providers.size() == 7);
    CHECK(merged.find("anthropic")->recommendedModel == "claude-haiku-5-5");
    CHECK(merged.find("mine") != nullptr);
    CHECK(merged.find("openai")->recommendedModel.empty()); // untouched
}

TEST_CASE("the choice of the musician beats the entry, the entry beats the defaults", "[ai-models]") {
    const auto config = parseModelsConfig(shippedText());
    const auto& anthropic = *config.find("anthropic");
    const auto& ollama = *config.find("ollama");
    ProviderChoice none;
    CHECK(effectiveModel(anthropic, none) == "claude-sonnet-5-5");
    CHECK(effectiveModel(ollama, none).empty());
    CHECK(effectiveTimeout(anthropic, none) == 30);
    CHECK(effectiveTimeout(ollama, none) == 600);
    CHECK(effectiveMaxTokens(none) == 4096);
    CHECK(effectiveBaseUrl(ollama, none) == "http://localhost:11434/v1");
    ProviderChoice chosen;
    chosen.model = "llama3";
    chosen.timeoutSeconds = 200;
    chosen.maxTokens = 8000;
    chosen.baseUrl = "http://localhost:9999/v1";
    CHECK(effectiveModel(ollama, chosen) == "llama3");
    CHECK(effectiveTimeout(ollama, chosen) == 200);
    CHECK(effectiveMaxTokens(chosen) == 8000);
    CHECK(effectiveBaseUrl(ollama, chosen) == "http://localhost:9999/v1");
}

TEST_CASE("the factory builds the provider of the entry", "[ai-models]") {
    const auto config = parseModelsConfig(shippedText());
    const auto client = std::make_shared<NullClient>();
    const auto anthropic = makeProvider(*config.find("anthropic"), {}, "key", client);
    REQUIRE(anthropic != nullptr);
    CHECK(anthropic->info().id == "anthropic");
    CHECK(anthropic->info().schemaSupport == SchemaSupport::Enforced);
    CHECK(anthropic->info().models.size() == 3);

    const auto ollama = makeProvider(*config.find("ollama"), {}, "", client);
    CHECK(ollama->info().id == "ollama");
    CHECK(ollama->info().schemaSupport == SchemaSupport::Enforced);

    const auto custom = makeProvider(*config.find("custom"), {}, "", client);
    CHECK(custom->info().schemaSupport == SchemaSupport::JsonOnly);
    ProviderChoice expert;
    expert.schemaLevel = "enforced"; // only by manual choice (SPEC 7.2)
    CHECK(makeProvider(*config.find("custom"), expert, "", client)->info().schemaSupport == SchemaSupport::Enforced);
    expert.schemaLevel = "prompt";
    CHECK(makeProvider(*config.find("openai"), expert, "", client)->info().schemaSupport == SchemaSupport::PromptOnly);
    expert.schemaLevel = "garbage";
    CHECK(makeProvider(*config.find("openai"), expert, "", client)->info().schemaSupport == SchemaSupport::Enforced);
}
