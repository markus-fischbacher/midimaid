#include "core/GlobalSettings.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

using namespace mm::core;

TEST_CASE("the defaults are A minor, basic level, strength 30 and a 1/16 grid", "[global-settings]") {
    const GlobalSettings defaults;
    CHECK_FALSE(defaults.expert);
    CHECK(defaults.startRoot == 9);
    CHECK(defaults.startScaleId == "natural_minor");
    CHECK(defaults.variationStrength == 30);
    CHECK(defaults.grid == EditGrid{16, false});
    CHECK_FALSE(defaults.snapChromatic);
    CHECK(sanitize(defaults) == defaults);
}

TEST_CASE("settings survive writing and reading", "[global-settings]") {
    GlobalSettings settings;
    settings.expert = true;
    settings.startRoot = 3;
    settings.startScaleId = "phrygian";
    settings.variationStrength = 72;
    settings.grid = EditGrid{8, true};
    settings.snapChromatic = true;
    REQUIRE(findScale("phrygian") != nullptr);
    const auto text = toJson(settings);
    CHECK(text.find("\"version\": 1") != std::string::npos);
    CHECK(parseGlobalSettings(text) == settings);
}

TEST_CASE("a missing, unreadable or wrongly typed file gives the defaults", "[global-settings]") {
    const GlobalSettings defaults;
    for (const char* text : {"", "   ", "not json", "[]", "42", "null", "{", "{\"version\": 1", "\"text\""}) {
        INFO(text);
        CHECK(parseGlobalSettings(text) == defaults);
    }
    // no version, or one below 1, or not a whole number: not a file of ours
    for (const char* text : {"{\"expert\": true}", "{\"version\": 0, \"expert\": true}",
                             "{\"version\": -3, \"expert\": true}", "{\"version\": \"1\", \"expert\": true}",
                             "{\"version\": 1.5, \"expert\": true}", "{\"version\": null, \"expert\": true}"}) {
        INFO(text);
        CHECK(parseGlobalSettings(text) == defaults);
    }
}

TEST_CASE("a bad field gives its default and leaves the others", "[global-settings]") {
    const auto settings = parseGlobalSettings(R"({
        "version": 1, "expert": "yes", "startRoot": 12, "startScale": "klingon", "variationStrength": 150,
        "gridDivision": 7, "gridTriplet": true, "snapChromatic": 1})");
    const GlobalSettings defaults;
    CHECK(settings.expert == defaults.expert);
    CHECK(settings.startRoot == defaults.startRoot);
    CHECK(settings.startScaleId == defaults.startScaleId);
    CHECK(settings.variationStrength == defaults.variationStrength);
    CHECK(settings.grid.division == defaults.grid.division);
    CHECK(settings.grid.triplet); // the one good field
    CHECK(settings.snapChromatic == defaults.snapChromatic);
    const auto good =
        parseGlobalSettings(R"({"version": 1, "startRoot": 0, "variationStrength": 0, "gridDivision": 32})");
    CHECK(good.startRoot == 0);
    CHECK(good.variationStrength == 0);
    CHECK(good.grid.division == 32);
    CHECK(parseGlobalSettings(R"({"version": 1, "startRoot": -1, "variationStrength": -1})") == defaults);
}

TEST_CASE("a file of a later version and unknown fields load as far as understood", "[global-settings]") {
    const auto settings =
        parseGlobalSettings(R"({"version": 7, "expert": true, "startRoot": 5, "future": {"a": [1]}})");
    CHECK(settings.expert);
    CHECK(settings.startRoot == 5);
}

TEST_CASE("sanitize brings invalid values into range", "[global-settings]") {
    GlobalSettings settings;
    settings.startRoot = 40;
    settings.startScaleId = "nope";
    settings.variationStrength = 400;
    settings.grid.division = 5;
    const auto clean = sanitize(settings);
    CHECK(clean.startRoot == 9);
    CHECK(clean.startScaleId == "natural_minor");
    CHECK(clean.variationStrength == 100);
    CHECK(clean.grid.division == 16);
    settings.variationStrength = -5;
    CHECK(sanitize(settings).variationStrength == 0);
    // writing sanitizes, too
    CHECK(parseGlobalSettings(toJson(settings)) == sanitize(settings));
}

TEST_CASE("the drum mapping line is kept, cleaned and survives writing", "[global-settings]") {
    GlobalSettings settings;
    CHECK(settings.drumMap.empty());
    settings.drumMap = "60=kick, 61=closed_hat";
    CHECK(parseGlobalSettings(toJson(settings)).drumMap == "60=kick, 61=closed_hat");
    // what cannot be read is cleaned out, also from a file
    GlobalSettings messy;
    messy.drumMap = " 60 = kick ; 999=kick,x=ride, 61=ride";
    CHECK(sanitize(messy).drumMap == "60=kick, 61=ride");
    CHECK(parseGlobalSettings("{\"version\":1,\"drumMap\":\"60=kick;70=tuba\"}").drumMap == "60=kick");
    CHECK(parseGlobalSettings("{\"version\":1,\"drumMap\":5}").drumMap.empty());
}
