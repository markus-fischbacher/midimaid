#include "core/ChordSymbol.h"
#include "core/Constraints.h"
#include "core/Pattern.h"
#include "core/Random.h"
#include "core/StyleProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace mm::core;
using nlohmann::json;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

namespace {

std::string readFile(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

StyleProfile loadShipped(const std::string& name) {
    const auto result = loadStyleProfile(readFile(name));
    INFO(name << ": " << result.error);
    REQUIRE(result.ok());
    return *result.profile;
}

using Weights = std::vector<std::pair<std::string, int>>;

Weights weightsOf(const std::vector<WeightedId>& list) {
    Weights result;
    for (const WeightedId& entry : list) {
        result.emplace_back(entry.id, entry.weight);
    }
    return result;
}

std::vector<std::vector<Chord>> progressions(std::initializer_list<std::initializer_list<const char*>> lists) {
    std::vector<std::vector<Chord>> result;
    for (const auto& list : lists) {
        std::vector<Chord> chords;
        for (const char* symbol : list) {
            chords.push_back(*parseChordSymbol(symbol));
        }
        result.push_back(chords);
    }
    return result;
}

StyleLoadResult loadWith(const std::function<void(json&)>& mutate) {
    json document = json::parse(readFile("peak_time"));
    mutate(document);
    return loadStyleProfileJson(document);
}

} // namespace

TEST_CASE("all shipped profiles load", "[core][style]") {
    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile profile = loadShipped(name);
        CHECK(profile.id == name);
        CHECK(profile.version == 1);
        CHECK(validateStyleProfile(profile).empty());
    }
}

TEST_CASE("Peak Time matches STYLES.md 2", "[core][style]") {
    const StyleProfile p = loadShipped("peak_time");
    CHECK(p.nameDe == "Peak Time / Driving");
    CHECK(p.tempoMin == 128);
    CHECK(p.tempoMax == 135);
    CHECK(weightsOf(p.scales) ==
          Weights{{"natural_minor", 3}, {"phrygian", 2}, {"dorian", 2}, {"minor_pentatonic", 1}});
    CHECK(p.chromaticDefaultPercent == 10);
    CHECK(weightsOf(p.harmony.modes) == Weights{{"static", 50}, {"two_chord", 40}, {"four_chord", 10}});
    CHECK(p.harmony.progressions == progressions({{"i", "bVII"}, {"i", "bVI"}, {"i", "iv"}}));
    CHECK(p.harmony.chordLengthBarsMin == 4);
    CHECK(p.harmony.chordLengthBarsMax == 8);
    CHECK(p.kickDefault == "4otf");
    CHECK(p.kickVariants.empty());
    CHECK(p.bass.range == Range{28, 52});
    CHECK(p.bass.kickClearanceTicks == 120);
    CHECK(p.bass.swingDefault == 0.52f);
    CHECK(p.bass.movement == BassMovement{70, 25, 5, false});
    CHECK(weightsOf(p.bass.archetypes) == Weights{{"rolling16", 4}, {"offbeat", 3}, {"gallop", 3}});
    CHECK(p.melody.range == Range{55, 88});
    CHECK(p.melody.voicingRange == Range{55, 79});
    CHECK(p.melody.chordMemory);
    CHECK(p.melody.swingDefault == 0.55f);
    CHECK(weightsOf(p.melody.archetypes) ==
          Weights{{"hypnotic_motif", 4}, {"stabs", 3}, {"arp", 2}, {"polymeter_seq", 3}, {"acid_siren", 1}});
    CHECK(weightsOf(p.melody.chordColors) == Weights{{"min", 40}, {"fifth", 30}, {"min7", 20}, {"sus", 10}});
    CHECK(p.quality == QualityProfile{50, 3, 1, 2, 2, 2, 1});
}

TEST_CASE("Melodic Techno matches STYLES.md 3", "[core][style]") {
    const StyleProfile p = loadShipped("melodic_techno");
    CHECK(p.tempoMin == 120);
    CHECK(p.tempoMax == 128);
    CHECK(weightsOf(p.scales) ==
          Weights{{"natural_minor", 3}, {"dorian", 2}, {"harmonic_minor", 1}, {"phrygian_dominant", 1}});
    CHECK(p.chromaticDefaultPercent == 0);
    CHECK(weightsOf(p.harmony.modes) == Weights{{"four_chord", 60}, {"two_chord", 30}, {"static", 10}});
    CHECK(p.harmony.progressions == progressions({{"i", "bVI", "bIII", "bVII"},
                                                  {"i", "iv", "bVI", "V"},
                                                  {"i", "bVII", "bVI", "bVII"},
                                                  {"i", "bVI", "iv", "bVII"}}));
    CHECK(p.harmony.chordLengthBarsMin == 1);
    CHECK(p.harmony.chordLengthBarsMax == 2);
    CHECK(p.bass.movement == BassMovement{35, 30, 35, true});
    CHECK(weightsOf(p.bass.archetypes) == Weights{{"rolling16_harmonic", 4}, {"offbeat_changes", 3}, {"long_tied", 2}});
    CHECK(p.bass.swingDefault == 0.50f);
    CHECK(weightsOf(p.melody.archetypes) ==
          Weights{{"chord_arp", 4}, {"lead_phrase", 3}, {"call_response", 3}, {"pluck_seq", 3}});
    CHECK_FALSE(p.melody.chordMemory); // voice leading instead of chord memory
    CHECK(p.melody.swingDefault == 0.52f);
    CHECK(weightsOf(p.melody.chordColors) == Weights{{"min7", 25}, {"min9", 25}, {"min", 30}, {"sus", 20}});
}

TEST_CASE("Hard / Industrial matches STYLES.md 4", "[core][style]") {
    const StyleProfile p = loadShipped("hard_industrial");
    CHECK(p.tempoMin == 140);
    CHECK(p.tempoMax == 165);
    CHECK(weightsOf(p.scales) == Weights{{"phrygian", 3}, {"natural_minor", 1}, {"locrian", 1}});
    CHECK(p.chromaticDefaultPercent == 35);
    CHECK(weightsOf(p.harmony.modes) == Weights{{"static", 80}, {"two_chord", 20}});
    CHECK(p.harmony.progressions == progressions({{"i", "bII"}}));
    CHECK(p.kickDefault == "4otf");
    CHECK(p.kickVariants == std::vector<std::string>{"broken_a", "broken_b"});
    CHECK(p.bass.range == Range{28, 50});
    CHECK(p.bass.movement == BassMovement{90, 5, 5, false});
    CHECK(weightsOf(p.bass.archetypes) == Weights{{"rumble", 4}, {"hard_offbeat", 3}, {"roll16_aggressive", 3}});
    CHECK(weightsOf(p.melody.archetypes) ==
          Weights{{"aggro_stabs", 3}, {"atonal_motif", 3}, {"acid_siren", 3}, {"sparse_hits", 2}});
    CHECK(p.melody.chordMemory);
    CHECK(weightsOf(p.melody.chordColors) == Weights{{"cluster", 40}, {"fifth", 40}, {"min", 20}});
}

TEST_CASE("the profiles feed the register and the constraint settings", "[core][style]") {
    const StyleProfile hard = loadShipped("hard_industrial");
    const auto bassRange = effectiveRange(hard.registerProfile(), VoiceRole::Bass, "rumble", VoicingSettings{}, 0);
    REQUIRE(bassRange.has_value());
    CHECK(*bassRange == Range{28, 50});

    Pattern pattern = makeEmptyPattern(1, "hard_industrial");
    const auto settings = ConstraintSettings::forPattern(pattern, hard.registerProfile());
    CHECK(settings.voices[0].rangeHigh == 50);
    CHECK(settings.voices[1].rangeHigh == 88);

    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile profile = loadShipped(name);
        CHECK(effectiveRange(profile.registerProfile(), VoiceRole::Bass, "", VoicingSettings{}, 0).has_value());
        CHECK(effectiveRange(profile.registerProfile(), VoiceRole::Melody, "", VoicingSettings{}, 0).has_value());
    }
}

TEST_CASE("optional fields and unknown fields", "[core][style]") {
    const auto noEnglish = loadWith([](json& d) { d["name"].erase("en"); });
    REQUIRE(noEnglish.ok());
    CHECK(noEnglish.profile->nameEn == noEnglish.profile->nameDe);

    const auto noOptional = loadWith([](json& d) {
        d.erase("kick_variants");
        d["melody"]["voicing"].erase("chord_memory");
    });
    REQUIRE(noOptional.ok());
    CHECK_FALSE(noOptional.profile->melody.chordMemory);

    const auto unknown = loadWith([](json& d) {
        d["future"] = {{"x", 1}};
        d["bass"]["future_field"] = true;
        d["quality"]["weights"]["novelty"] = 2; // a criterion of v1.1
        d["melody"]["voicing"]["future"] = "later";
    });
    REQUIRE(unknown.ok());
    CHECK(*unknown.profile == loadShipped("peak_time"));
}

TEST_CASE("shares and ints from the JSON numbers", "[core][style]") {
    const auto result = loadWith([](json& d) {
        d["chromatic_default"] = 0.35;
        d["bass"]["movement"] = {{"root", 0.5}, {"fifth_octave", 0.25}, {"passing", 0.25}};
        d["bass"]["swing_default"] = 0.6;
    });
    REQUIRE(result.ok());
    CHECK(result.profile->chromaticDefaultPercent == 35);
    CHECK(result.profile->bass.movement.rootPercent == 50);
    CHECK(result.profile->bass.swingDefault == static_cast<float>(0.6));
}

TEST_CASE("invalid profiles are rejected with the path of the problem", "[core][style]") {
    struct Case {
        const char* what;
        const char* pathPart;
        std::function<void(json&)> mutate;
    };
    const std::vector<Case> cases = {
        {"id with upper case", "id", [](json& d) { d["id"] = "Peak"; }},
        {"id empty", "id", [](json& d) { d["id"] = ""; }},
        {"id missing", "id", [](json& d) { d.erase("id"); }},
        {"version zero", "version", [](json& d) { d["version"] = 0; }},
        {"version as string", "version", [](json& d) { d["version"] = "1"; }},
        {"german name empty", "name.de", [](json& d) { d["name"]["de"] = ""; }},
        {"name missing", "name", [](json& d) { d.erase("name"); }},
        {"tempo descending", "tempo", [](json& d) { d["tempo"] = {135, 128}; }},
        {"tempo too slow", "tempo", [](json& d) { d["tempo"] = {30, 40}; }},
        {"tempo too fast", "tempo", [](json& d) { d["tempo"] = {200, 300}; }},
        {"tempo with three numbers", "tempo", [](json& d) { d["tempo"] = {1, 2, 3}; }},
        {"scale unknown", "scales[0].id", [](json& d) { d["scales"][0]["id"] = "lydian"; }},
        {"scale weight zero", "scales[0].w", [](json& d) { d["scales"][0]["w"] = 0; }},
        {"scale duplicate", "scales[1].id", [](json& d) { d["scales"][1]["id"] = "natural_minor"; }},
        {"scales empty", "scales", [](json& d) { d["scales"] = json::array(); }},
        {"scales missing", "scales", [](json& d) { d.erase("scales"); }},
        {"scale weight float", "scales[0].w", [](json& d) { d["scales"][0]["w"] = 1.5; }},
        {"chromatic too high", "chromatic_default", [](json& d) { d["chromatic_default"] = 1.5; }},
        {"chromatic negative", "chromatic_default", [](json& d) { d["chromatic_default"] = -0.1; }},
        {"chromatic as string", "chromatic_default", [](json& d) { d["chromatic_default"] = "10"; }},
        {"mode unknown", "harmony.modes[0].id", [](json& d) { d["harmony"]["modes"][0]["id"] = "three_chord"; }},
        {"modes all zero", "harmony.modes",
         [](json& d) {
             for (auto& mode : d["harmony"]["modes"]) {
                 mode["w"] = 0;
             }
         }},
        {"chord changes without progressions", "harmony.progressions",
         [](json& d) { d["harmony"]["progressions"] = json::array(); }},
        {"progression with an invalid symbol", "harmony.progressions[1][1]",
         [](json& d) { d["harmony"]["progressions"][1][1] = "bVIII"; }},
        {"progression with a unicode symbol", "harmony.progressions[0][1]",
         [](json& d) {
             d["harmony"]["progressions"][0][1] = "\xe2\x99\xad"
                                                  "VII";
         }},
        {"progression with one chord", "harmony.progressions[0]",
         [](json& d) { d["harmony"]["progressions"][0] = json::array({"i"}); }},
        {"progression with five chords", "harmony.progressions[0]",
         [](json& d) { d["harmony"]["progressions"][0] = json::array({"i", "i", "i", "i", "i"}); }},
        {"progression not an array", "harmony.progressions[0]", [](json& d) { d["harmony"]["progressions"][0] = "i"; }},
        {"chord length 3", "harmony.chord_length_bars", [](json& d) { d["harmony"]["chord_length_bars"] = {3, 4}; }},
        {"chord length descending", "harmony.chord_length_bars",
         [](json& d) { d["harmony"]["chord_length_bars"] = {8, 4}; }},
        {"unknown kick grid", "kick_default", [](json& d) { d["kick_default"] = "disco"; }},
        {"custom kick grid as default", "kick_default", [](json& d) { d["kick_default"] = "custom"; }},
        {"unknown kick variant", "kick_variants[0]", [](json& d) { d["kick_variants"] = {"disco"}; }},
        {"duplicate kick variant", "kick_variants[1]", [](json& d) { d["kick_variants"] = {"broken_a", "broken_a"}; }},
        {"bass range widened below", "bass.range", [](json& d) { d["bass"]["range"] = {20, 52}; }},
        {"bass range widened above", "bass.range", [](json& d) { d["bass"]["range"] = {28, 60}; }},
        {"bass range too narrow", "bass.range", [](json& d) { d["bass"]["range"] = {30, 40}; }},
        {"bass range with one number", "bass.range", [](json& d) { d["bass"]["range"] = {30}; }},
        {"bass range as string", "bass.range", [](json& d) { d["bass"]["range"] = "28-52"; }},
        {"kick clearance unknown", "bass.kick_clearance", [](json& d) { d["bass"]["kick_clearance"] = "1/8"; }},
        {"kick clearance as number", "bass.kick_clearance", [](json& d) { d["bass"]["kick_clearance"] = 120; }},
        {"bass swing too high", "bass.swing_default", [](json& d) { d["bass"]["swing_default"] = 0.9; }},
        {"bass swing below 50 percent", "bass.swing_default", [](json& d) { d["bass"]["swing_default"] = 0.4; }},
        {"movement not adding up", "bass.movement", [](json& d) { d["bass"]["movement"]["root"] = 0.6; }},
        {"movement negative", "bass.movement",
         [](json& d) { d["bass"]["movement"] = {{"root", 1.2}, {"fifth_octave", -0.2}, {"passing", 0.0}}; }},
        {"movement key missing", "bass.movement.passing", [](json& d) { d["bass"]["movement"].erase("passing"); }},
        {"bass archetype id with upper case", "bass.archetypes[0].id",
         [](json& d) { d["bass"]["archetypes"][0]["id"] = "Rolling16"; }},
        {"bass archetype weight zero", "bass.archetypes[0].w", [](json& d) { d["bass"]["archetypes"][0]["w"] = 0; }},
        {"bass archetypes empty", "bass.archetypes", [](json& d) { d["bass"]["archetypes"] = json::array(); }},
        {"melody range widened", "melody.range", [](json& d) { d["melody"]["range"] = {50, 88}; }},
        {"melody voicing range widened", "melody.voicing.range",
         [](json& d) { d["melody"]["voicing"]["range"] = {55, 90}; }},
        {"melody voicing missing", "melody.voicing", [](json& d) { d["melody"].erase("voicing"); }},
        {"melody swing too high", "melody.swing_default", [](json& d) { d["melody"]["swing_default"] = 0.8; }},
        {"chord colour unknown", "melody.chord_colors[0].id",
         [](json& d) { d["melody"]["chord_colors"][0]["id"] = "dim7"; }},
        {"chord colours empty", "melody.chord_colors", [](json& d) { d["melody"]["chord_colors"] = json::array(); }},
        {"min score too high", "quality.min_score", [](json& d) { d["quality"]["min_score"] = 101; }},
        {"quality weights all zero", "quality.weights",
         [](json& d) {
             for (auto& weight : d["quality"]["weights"]) {
                 weight = 0;
             }
         }},
        {"quality weight negative", "quality.weights", [](json& d) { d["quality"]["weights"]["kick"] = -1; }},
        {"quality weight missing", "quality.weights.motif", [](json& d) { d["quality"]["weights"].erase("motif"); }},
        {"quality missing", "quality", [](json& d) { d.erase("quality"); }},
    };
    for (const auto& c : cases) {
        INFO(c.what);
        const auto result = loadWith(c.mutate);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.error.empty());
        CHECK(result.error.find(c.pathPart) != std::string::npos);
    }
}

TEST_CASE("validateStyleProfile checks a loaded profile directly", "[core][style]") {
    StyleProfile p = loadShipped("peak_time");
    CHECK(validateStyleProfile(p).empty());
    p.bass.movement.rootPercent = 71;
    CHECK_FALSE(validateStyleProfile(p).empty());
    p = loadShipped("peak_time");
    p.melody.range = {40, 88};
    CHECK_FALSE(validateStyleProfile(p).empty());
}

TEST_CASE("garbage input never crashes", "[core][style]") {
    for (const char* text :
         {"", " ", "not json", "{", "[]", "null", "42", "\"text\"", "{}", "{\"id\":\"x\"}", "\xff\xfe"}) {
        const auto result = loadStyleProfile(text);
        CHECK_FALSE(result.ok());
        CHECK_FALSE(result.error.empty());
    }
    CHECK_FALSE(loadStyleProfile(std::string(100000, '[') + std::string(100000, ']')).ok());
    CHECK_FALSE(loadStyleProfile(std::string(100000, '[')).ok());
}

TEST_CASE("random changes to a profile never crash and never yield an invalid profile", "[core][style][property]") {
    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const json original = json::parse(readFile(name));
        const json flat = original.flatten();
        std::vector<std::string> pointers;
        for (const auto& item : flat.items()) {
            pointers.push_back(item.key());
        }
        auto rng = Pcg32::fromSeed(std::hash<std::string>{}(name));
        for (int i = 0; i < 400; ++i) {
            json document = original;
            const int changes = 1 + static_cast<int>(rng.bounded(3));
            for (int c = 0; c < changes; ++c) {
                const std::string& pointer = pointers[rng.bounded(static_cast<uint32_t>(pointers.size()))];
                json value;
                switch (rng.bounded(9)) {
                case 0:
                    value = nullptr;
                    break;
                case 1:
                    value = "x";
                    break;
                case 2:
                    value = -1;
                    break;
                case 3:
                    value = 0;
                    break;
                case 4:
                    value = 100000;
                    break;
                case 5:
                    value = 0.5;
                    break;
                case 6:
                    value = true;
                    break;
                case 7:
                    value = json::array({1, 2, 3});
                    break;
                default:
                    value = json::object();
                    break;
                }
                document[json::json_pointer(pointer)] = value;
            }
            const auto result = loadStyleProfileJson(document);
            INFO(name << " #" << i);
            if (result.ok()) {
                REQUIRE(validateStyleProfile(*result.profile).empty());
            } else {
                REQUIRE_FALSE(result.error.empty());
            }
        }
    }
}
