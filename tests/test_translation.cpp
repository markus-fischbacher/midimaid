#include "core/TextKeys.h"
#include "core/Theory.h"
#include "core/Translation.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

using mm::core::Translation;

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

Translation loadLanguage(const std::string& name) {
    return Translation::fromJson(readFile(std::string(MIDIMAID_RESOURCE_DIR) + "/i18n/" + name + ".json"));
}

} // namespace

TEST_CASE("a text is returned for its key and the key itself when it is missing", "[i18n]") {
    const auto table = Translation::fromJson(R"({"a": "Alpha"})");
    CHECK(table.tr("a") == "Alpha");
    CHECK(table.tr("b") == "b");
    CHECK(table.has("a"));
    CHECK_FALSE(table.has("b"));
}

TEST_CASE("placeholders are replaced, unknown ones stay, values are not searched again", "[i18n]") {
    const auto table =
        Translation::fromJson(R"json({"a": "{n} of {total} ({x})", "b": "{n}{n}", "c": "open { brace"})json");
    CHECK(table.tr("a", {{"n", "3"}, {"total", "16"}}) == "3 of 16 ({x})");
    CHECK(table.tr("b", {{"n", "{n}"}}) == "{n}{n}");
    CHECK(table.tr("c", {{"n", "1"}}) == "open { brace");
    CHECK(table.tr("missing", {{"n", "1"}}) == "missing");
}

TEST_CASE("an entry that is not a text and broken JSON are reported", "[i18n]") {
    const auto table = Translation::fromJson(R"({"a": "x", "b": 3})");
    CHECK(table.has("a"));
    CHECK_FALSE(table.has("b"));
    CHECK(table.problems().size() == 1);
    const auto broken = Translation::fromJson("[1, 2");
    CHECK(broken.size() == 0);
    CHECK(broken.problems().size() == 1);
    CHECK(Translation::fromJson("[1]").problems().size() == 1);
}

TEST_CASE("the German table has exactly the keys the code asks for and no empty text", "[i18n]") {
    const auto german = loadLanguage("de");
    CHECK(german.problems().empty());
    std::set<std::string> asked;
    for (const auto key : mm::core::text::kAllKeys) {
        asked.insert(std::string(key));
    }
    for (const auto& scale : mm::core::allScales()) {
        asked.insert(mm::core::text::scaleKey(scale.id));
    }
    CHECK(asked.size() == mm::core::text::kAllKeys.size() + mm::core::allScales().size()); // no key twice
    for (const auto& key : asked) {
        INFO("missing in de.json: " << key);
        CHECK(german.has(key));
    }
    for (const auto& key : german.keys()) {
        INFO("not used by the code: " << key);
        CHECK(asked.count(key) == 1);
        CHECK_FALSE(german.tr(key).empty());
    }
}

TEST_CASE("every language file has the keys of the German one", "[i18n]") {
    const auto german = loadLanguage("de").keys();
    for (const char* other : {"en"}) { // v1.1 adds en.json; a file that does not exist yet is skipped
        std::ifstream probe(std::string(MIDIMAID_RESOURCE_DIR) + "/i18n/" + other + ".json");
        if (!probe) {
            continue;
        }
        CHECK(loadLanguage(other).keys() == german);
    }
}

TEST_CASE("the placeholders of a text are the same in every use", "[i18n]") {
    // A text with a placeholder the code does not fill would show "{name}" on screen.
    const auto german = loadLanguage("de");
    const std::set<std::string> known = {"n",   "note",   "name",   "value", "slot",     "style",
                                         "key", "scale",  "length", "seed",  "winner",   "voice",
                                         "m",   "reason", "score",  "level", "provider", "list"};
    for (const auto& key : german.keys()) {
        const auto text = german.tr(key);
        for (size_t i = text.find('{'); i != std::string::npos; i = text.find('{', i + 1)) {
            const auto close = text.find('}', i);
            REQUIRE(close != std::string::npos);
            INFO(key);
            CHECK(known.count(text.substr(i + 1, close - i - 1)) == 1);
        }
    }
}
