#include "core/GlobalSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace mm::core;
using nlohmann::json;

namespace {

AiSettings sample() {
    AiSettings ai;
    ai.activeProvider = "anthropic";
    ai.logPrompts = true;
    ai.cloudConsent = {"anthropic", "openai"};
    ai.providers["anthropic"] = {"claude-sonnet-5-5", "", 45, 8000, ""};
    ai.providers["custom"] = {"my-model", "http://localhost:9000/v1", 0, 0, "enforced"};
    return ai;
}

} // namespace

TEST_CASE("the AI settings are saved and read back", "[ai-settings]") {
    GlobalSettings settings;
    settings.ai = sample();
    const auto text = toJson(settings);
    const auto loaded = parseGlobalSettings(text);
    CHECK(loaded.ai == settings.ai);
    CHECK(loaded == settings);
}

TEST_CASE("by default no provider is active and nothing is logged", "[ai-settings]") {
    const GlobalSettings defaults;
    CHECK(defaults.ai.activeProvider.empty());
    CHECK_FALSE(defaults.ai.logPrompts);
    CHECK(defaults.ai.cloudConsent.empty());
    CHECK(defaults.ai.providers.empty());
}

TEST_CASE("a settings file without the AI part loads the defaults", "[ai-settings]") {
    const auto loaded = parseGlobalSettings(R"({"version":1,"expert":true})");
    CHECK(loaded.expert);
    CHECK(loaded.ai == AiSettings{});
}

TEST_CASE("no key is ever written to the settings file", "[ai-settings]") {
    // The settings have no field for a key; even a key put into a text field does not turn into one.
    GlobalSettings settings;
    settings.ai = sample();
    const auto text = toJson(settings);
    const auto root = json::parse(text);
    for (const auto& [id, provider] : root["ai"]["providers"].items()) {
        for (const auto& [name, value] : provider.items()) {
            CHECK(name != "apiKey");
            CHECK(name != "key");
        }
    }
    auto read = parseGlobalSettings(R"({"version":1,"ai":{"providers":{"anthropic":{"apiKey":"sk-ant-SECRET"}}}})");
    CHECK(toJson(read).find("sk-ant-SECRET") == std::string::npos);
}

TEST_CASE("bad AI fields fall back one by one", "[ai-settings]") {
    const auto loaded = parseGlobalSettings(R"({"version":1,"ai":{
        "activeProvider": 5, "logPrompts": "yes", "cloudConsent": ["ok", 3, "bad id!", "x/y"],
        "providers": {"good": {"model": "m", "timeoutSeconds": "slow", "maxTokens": 9999999999},
                      "bad id": {"model": "x"}, "list": [1], "other": {"schemaLevel": "magic", "baseUrl": 7}}}})");
    CHECK(loaded.ai.activeProvider.empty());
    CHECK_FALSE(loaded.ai.logPrompts);
    CHECK(loaded.ai.cloudConsent == std::set<std::string>{"ok"});
    REQUIRE(loaded.ai.providers.count("good") == 1);
    CHECK(loaded.ai.providers.at("good").model == "m");
    CHECK(loaded.ai.providers.at("good").timeoutSeconds == 0);
    CHECK(loaded.ai.providers.at("good").maxTokens == 0);
    CHECK(loaded.ai.providers.count("bad id") == 0);
    CHECK(loaded.ai.providers.count("list") == 0);
    CHECK(loaded.ai.providers.at("other").schemaLevel.empty());
    CHECK(loaded.ai.providers.at("other").baseUrl.empty());
}

TEST_CASE("the AI settings are brought into range", "[ai-settings]") {
    GlobalSettings settings;
    settings.ai.activeProvider = "not valid!";
    settings.ai.providers["a"] = {std::string(1000, 'm'), std::string(2000, 'u'), 1, 999999, "json"};
    settings.ai.providers["b"] = {"", "", 100000, 10, "enforced"};
    const auto fixed = sanitize(settings);
    CHECK(fixed.ai.activeProvider.empty());
    CHECK(fixed.ai.providers.at("a").model.size() == 200);
    CHECK(fixed.ai.providers.at("a").baseUrl.size() == 500);
    CHECK(fixed.ai.providers.at("a").timeoutSeconds == 5);
    CHECK(fixed.ai.providers.at("a").maxTokens == 64000);
    CHECK(fixed.ai.providers.at("b").timeoutSeconds == 600);
    CHECK(fixed.ai.providers.at("b").maxTokens == 256);
    CHECK(fixed.ai.providers.at("b").schemaLevel == "enforced");
}

TEST_CASE("a model name is cut at a character boundary", "[ai-settings]") {
    GlobalSettings settings;
    settings.ai.providers["a"].model = std::string(199, 'a') + "ä" + "tail"; // "ä" is two bytes: 199 + 2 > 200
    const auto fixed = sanitize(settings);
    CHECK(fixed.ai.providers.at("a").model == std::string(199, 'a'));
}

TEST_CASE("only a limited number of providers and consents is kept", "[ai-settings]") {
    GlobalSettings settings;
    for (int i = 0; i < 40; ++i) {
        settings.ai.providers["p" + std::to_string(i)].model = "m";
        settings.ai.cloudConsent.insert("p" + std::to_string(i));
    }
    const auto fixed = sanitize(settings);
    CHECK(fixed.ai.providers.size() == 16);
    CHECK(fixed.ai.cloudConsent.size() == 16);
}
