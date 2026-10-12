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
    CHECK(defaults.middleC == 3);
    CHECK_FALSE(defaults.rollFold);
    CHECK(defaults.rollGuides); // the guides are on, the fold and the marker row are off (D-232)
    CHECK_FALSE(defaults.rollMarkers);
    CHECK(defaults.language == "auto");
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
    settings.middleC = 4;
    settings.rollFold = true;
    settings.rollGuides = false;
    settings.rollMarkers = true;
    settings.language = "en";
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
        "gridDivision": 7, "gridTriplet": true, "snapChromatic": 1, "rollFold": 1, "rollGuides": "no", "rollMarkers": null})");
    const GlobalSettings defaults;
    CHECK(settings.expert == defaults.expert);
    CHECK(settings.startRoot == defaults.startRoot);
    CHECK(settings.startScaleId == defaults.startScaleId);
    CHECK(settings.variationStrength == defaults.variationStrength);
    CHECK(settings.grid.division == defaults.grid.division);
    CHECK(settings.grid.triplet); // the one good field
    CHECK(settings.snapChromatic == defaults.snapChromatic);
    CHECK(settings.rollFold == defaults.rollFold);
    CHECK(settings.rollGuides == defaults.rollGuides);
    CHECK(settings.rollMarkers == defaults.rollMarkers);
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

// --- the keyboard transposition (SPEC 3.16, 8.4) ----------------------------------------------------------------

TEST_CASE("the transposition defaults are those of the specification", "[global-settings][transpose]") {
    const GlobalSettings defaults;
    CHECK(defaults.transpose == TransposeSettings{});
    CHECK(defaults.transpose.mode == TransposeMode::Chromatic);
    CHECK(defaults.transpose.behavior == TransposeBehavior::Hold);
    CHECK(defaults.transpose.timing == TransposeTiming::NextNote);
    CHECK(defaults.transpose.target == TransposeTarget::Both);
    CHECK(defaults.transpose.referenceNote == -1);
    CHECK(defaults.transpose.channel == 0);
    CHECK(defaults.transpose.exportTransposed);
    CHECK_FALSE(defaults.transpose.midiThru); // SPEC 3.17: not passed on unless asked
}

TEST_CASE("every transposition setting survives writing and reading", "[global-settings][transpose]") {
    for (const auto mode : {TransposeMode::Chromatic, TransposeMode::Diatonic}) {
        for (const auto behavior : {TransposeBehavior::Hold, TransposeBehavior::Momentary}) {
            for (const auto timing : {TransposeTiming::NextNote, TransposeTiming::NextBeat, TransposeTiming::NextBar}) {
                for (const auto target : {TransposeTarget::Both, TransposeTarget::Bass, TransposeTarget::Melody}) {
                    GlobalSettings settings;
                    settings.transpose = {mode, behavior, timing, target, 52, 9, false, true};
                    CHECK(parseGlobalSettings(toJson(settings)) == settings);
                }
            }
        }
    }
    GlobalSettings settings;
    settings.transpose.referenceNote = 0;
    settings.transpose.channel = 16;
    CHECK(parseGlobalSettings(toJson(settings)).transpose.referenceNote == 0);
    CHECK(parseGlobalSettings(toJson(settings)).transpose.channel == 16);
}

TEST_CASE("a file without transposition, or with bad transposition fields, gives the defaults field by field",
          "[global-settings][transpose]") {
    const TransposeSettings defaults;
    CHECK(parseGlobalSettings("{\"version\": 1, \"expert\": true}").transpose == defaults);
    const auto parsed = parseGlobalSettings(
        "{\"version\": 1, \"transpose\": {\"mode\": \"diatonic\", \"behavior\": 7, \"timing\": \"later\", "
        "\"target\": \"melody\", \"referenceNote\": \"A\", \"channel\": 99, \"export\": \"yes\", \"midiThru\": true, "
        "\"unknown\": 1}}");
    CHECK(parsed.transpose.mode == TransposeMode::Diatonic);
    CHECK(parsed.transpose.behavior == TransposeBehavior::Hold);       // not a text
    CHECK(parsed.transpose.timing == TransposeTiming::NextNote);       // an unknown text
    CHECK(parsed.transpose.target == TransposeTarget::Melody);
    CHECK(parsed.transpose.referenceNote == -1);                       // not a number
    CHECK(parsed.transpose.channel == 16);                             // limited
    CHECK(parsed.transpose.exportTransposed);                          // not a boolean
    CHECK(parsed.transpose.midiThru);
    // a transposition that is no object is left alone
    CHECK(parseGlobalSettings("{\"version\": 1, \"transpose\": 5}").transpose == defaults);
    CHECK(parseGlobalSettings("{\"version\": 1, \"transpose\": {\"referenceNote\": 500, \"channel\": -4}}")
              .transpose.referenceNote == 127);
    CHECK(parseGlobalSettings("{\"version\": 1, \"transpose\": {\"referenceNote\": -90}}").transpose.referenceNote == -1);
    CHECK(parseGlobalSettings("{\"version\": 1, \"transpose\": {\"channel\": -4}}").transpose.channel == 0);
}

TEST_CASE("sanitizing keeps the transposition within its limits", "[global-settings][transpose]") {
    GlobalSettings settings;
    settings.transpose.referenceNote = 400;
    settings.transpose.channel = 77;
    const auto clean = sanitize(settings);
    CHECK(clean.transpose.referenceNote == 127);
    CHECK(clean.transpose.channel == 16);
    settings.transpose.referenceNote = -30;
    settings.transpose.channel = -1;
    CHECK(sanitize(settings).transpose.referenceNote == -1);
    CHECK(sanitize(settings).transpose.channel == 0);
    // the words of the file
    CHECK(parseTransposeMode("nonsense") == TransposeMode::Chromatic);
    CHECK(parseTransposeMode(nullptr) == TransposeMode::Chromatic);
    CHECK(parseTransposeTiming(toString(TransposeTiming::NextBar)) == TransposeTiming::NextBar);
    CHECK(parseTransposeTiming(toString(TransposeTiming::NextBeat)) == TransposeTiming::NextBeat);
    CHECK(parseTransposeTarget(toString(TransposeTarget::Bass)) == TransposeTarget::Bass);
    CHECK(parseTransposeBehavior(toString(TransposeBehavior::Momentary)) == TransposeBehavior::Momentary);
}

TEST_CASE("the middle C convention is 3 or 4, anything else gives the default", "[global-settings]") {
    CHECK(parseGlobalSettings(R"({"version": 1, "middleC": 4})").middleC == 4);
    CHECK(parseGlobalSettings(R"({"version": 1, "middleC": 5})").middleC == 3);
    CHECK(parseGlobalSettings(R"({"version": 1, "middleC": "4"})").middleC == 3);
    GlobalSettings bad;
    bad.middleC = 9;
    CHECK(sanitize(bad).middleC == 3);
}

TEST_CASE("the language is auto, de or en, anything else gives auto", "[global-settings]") {
    CHECK(parseGlobalSettings(R"({"version": 1, "language": "en"})").language == "en");
    CHECK(parseGlobalSettings(R"({"version": 1, "language": "de"})").language == "de");
    CHECK(parseGlobalSettings(R"({"version": 1, "language": "auto"})").language == "auto");
    CHECK(parseGlobalSettings(R"({"version": 1, "language": "fr"})").language == "auto");
    CHECK(parseGlobalSettings(R"({"version": 1, "language": 3})").language == "auto");
    GlobalSettings bad;
    bad.language = "xx";
    CHECK(sanitize(bad).language == "auto");
}
