#include "core/Archetype.h"
#include "core/InstanceSettings.h"
#include "core/PatternGenerator.h"
#include "core/PatternJson.h"
#include "core/PatternValidation.h"
#include "core/TextKeys.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

using namespace mm::core;

namespace {

StyleProfile loadShipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

GenerationRequest requestWith(std::vector<VoiceRole> voices, uint64_t seed) {
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = seed;
    request.voices = std::move(voices);
    return request;
}

const std::vector<VoiceRole> kAllRoles{VoiceRole::Bass, VoiceRole::Melody, VoiceRole::Stabs, VoiceRole::Arp,
                                       VoiceRole::Counter};

bool oneOf(std::string_view id, std::initializer_list<std::string_view> family) {
    return std::find(family.begin(), family.end(), id) != family.end();
}

} // namespace

TEST_CASE("every voice role has a name that parses back, and every role but the bass is melodic", "[core][roles]") {
    std::set<std::string> names;
    for (const VoiceRole role : kAllRoles) {
        const std::string name = toString(role);
        names.insert(name);
        REQUIRE(parseVoiceRole(name).has_value());
        CHECK(*parseVoiceRole(name) == role);
        CHECK(isBassRole(role) == (role == VoiceRole::Bass));
        CHECK(isMelodicRole(role) == (role != VoiceRole::Bass));
        CHECK_FALSE(mm::core::text::roleTextKey(role).empty());
    }
    CHECK(names.size() == kAllRoles.size());
    CHECK_FALSE(parseVoiceRole("drums").has_value());
    CHECK_FALSE(parseVoiceRole("").has_value());
}

TEST_CASE("the new roles survive the pattern JSON, and an unknown role is refused", "[core][roles][json]") {
    const StyleProfile style = loadShipped("peak_time");
    const auto result = generatePattern(style, requestWith(kAllRoles, 5));
    REQUIRE(result.success);
    const std::string text = patternToString(result.pattern);
    const auto loaded = loadPattern(text);
    REQUIRE(loaded.ok());
    CHECK(*loaded.pattern == result.pattern);
    for (size_t i = 0; i < kAllRoles.size(); ++i) {
        CHECK(loaded.pattern->voices[i].role == kAllRoles[i]);
    }
    std::string broken = text;
    const std::string role = "\"role\":\"stabs\"";
    const size_t at = broken.find(role);
    REQUIRE(at != std::string::npos);
    broken.replace(at, role.size(), "\"role\":\"drums\"");
    CHECK_FALSE(loadPattern(broken).ok());
}

TEST_CASE("a role draws from the archetypes of its own family", "[core][roles][archetype]") {
    const StyleProfile style = loadShipped("melodic_techno");
    Pcg32 rng = Pcg32::fromSeed(3);
    std::set<std::string> stabs, arp, counter;
    for (int i = 0; i < 200; ++i) {
        stabs.insert(chooseArchetype(style, VoiceRole::Stabs, 50, rng));
        arp.insert(chooseArchetype(style, VoiceRole::Arp, 50, rng));
        counter.insert(chooseArchetype(style, VoiceRole::Counter, 50, rng));
    }
    for (const auto& id : stabs) {
        CHECK(oneOf(id, {"stabs", "aggro_stabs"}));
    }
    for (const auto& id : arp) {
        CHECK(oneOf(id, {"arp", "chord_arp", "pluck_seq"}));
    }
    for (const auto& id : counter) {
        CHECK(oneOf(id, {"call_response", "sparse_hits", "lead_phrase", "hypnotic_motif"}));
    }
    CHECK_FALSE(stabs.empty());
    CHECK_FALSE(arp.empty());
    CHECK_FALSE(counter.empty());
}

TEST_CASE("a style that names none of a role's archetypes still gives the role one", "[core][roles][archetype]") {
    StyleProfile style = loadShipped("peak_time");
    style.melody.archetypes = {{"hypnotic_motif", 5}}; // nothing for stabs or arp
    Pcg32 rng = Pcg32::fromSeed(9);
    for (int i = 0; i < 30; ++i) {
        CHECK(oneOf(chooseArchetype(style, VoiceRole::Stabs, 50, rng), {"stabs", "aggro_stabs"}));
        CHECK(oneOf(chooseArchetype(style, VoiceRole::Arp, 50, rng), {"arp", "chord_arp", "pluck_seq"}));
    }
    // bass and melody are not given a stand-in: the style decides
    style.melody.archetypes.clear();
    CHECK(chooseArchetype(style, VoiceRole::Melody, 50, rng).empty());
}

TEST_CASE("archetypes fit the roles by family: bass to bass, everything else melodic", "[core][roles][archetype]") {
    for (const Archetype& archetype : allArchetypes()) {
        CHECK(archetypeFitsRole(archetype, VoiceRole::Bass) == (archetype.role == VoiceRole::Bass));
        for (const VoiceRole role : {VoiceRole::Melody, VoiceRole::Stabs, VoiceRole::Arp, VoiceRole::Counter}) {
            CHECK(archetypeFitsRole(archetype, role) == (archetype.role == VoiceRole::Melody));
        }
    }
}

TEST_CASE("a request with five voices gives five voices that play, one channel each", "[core][roles][generator]") {
    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const StyleProfile style = loadShipped(name);
        const auto request = requestWith(kAllRoles, 21);
        const auto result = generatePattern(style, request);
        REQUIRE(result.success);
        const Pattern& pattern = result.pattern;
        INFO(name);
        REQUIRE(pattern.voices.size() == kAllRoles.size());
        CHECK(validatePattern(pattern).empty());
        std::set<int> channels;
        for (size_t i = 0; i < pattern.voices.size(); ++i) {
            CHECK(pattern.voices[i].role == kAllRoles[i]);
            CHECK_FALSE(pattern.voices[i].notes.empty());
            CHECK_FALSE(pattern.voices[i].archetypeId.empty());
            channels.insert(pattern.voices[i].midiChannel);
        }
        CHECK(channels.size() == kAllRoles.size());
        CHECK(oneOf(pattern.voices[2].archetypeId, {"stabs", "aggro_stabs"}));
        CHECK(oneOf(pattern.voices[3].archetypeId, {"arp", "chord_arp", "pluck_seq"}));
        CHECK(oneOf(pattern.voices[4].archetypeId, {"call_response", "sparse_hits", "lead_phrase", "hypnotic_motif"}));
        CHECK(generatePattern(style, request).pattern == pattern); // the same request, the same pattern
    }
}

TEST_CASE("the manual melody archetype belongs to the melody voice only", "[core][roles][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestWith(kAllRoles, 4);
    request.melodyArchetype = "acid_siren";
    const Pattern pattern = generateCandidate(style, request, 4);
    CHECK(pattern.voices[1].archetypeId == "acid_siren");
    CHECK(pattern.voices[2].archetypeId != "acid_siren");
    CHECK(pattern.voices[3].archetypeId != "acid_siren");
    CHECK(pattern.voices[4].archetypeId != "acid_siren");
}

TEST_CASE("addVoice generates only the new voice and keeps the others as they are", "[core][roles][generator]") {
    const StyleProfile style = loadShipped("melodic_techno");
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 8;
    const auto base = generatePattern(style, request);
    REQUIRE(base.success);
    for (const VoiceRole role : {VoiceRole::Stabs, VoiceRole::Arp, VoiceRole::Counter}) {
        Pattern pattern = base.pattern;
        REQUIRE(addVoice(pattern, role, style, request, 77));
        REQUIRE(pattern.voices.size() == 3);
        CHECK(pattern.voices[2].role == role);
        CHECK(pattern.voices[2].midiChannel == 3);
        CHECK_FALSE(pattern.voices[2].notes.empty());
        CHECK(validatePattern(pattern).empty());
        // bass and melody are untouched, as is the harmony; the new notes use ids that are still free
        CHECK(pattern.voices[0] == base.pattern.voices[0]);
        CHECK(pattern.voices[1] == base.pattern.voices[1]);
        CHECK(pattern.context == base.pattern.context);
        for (const Note& note : pattern.voices[2].notes) {
            CHECK(note.id >= base.pattern.nextNoteId);
        }
        Pattern again = base.pattern;
        REQUIRE(addVoice(again, role, style, request, 77));
        CHECK(again == pattern);
    }
}

TEST_CASE("addVoice leaves the pattern alone at eight voices", "[core][roles][generator]") {
    const StyleProfile style = loadShipped("peak_time");
    GenerationRequest request = requestWith(std::vector<VoiceRole>(kMaxVoices, VoiceRole::Arp), 2);
    request.voices[0] = VoiceRole::Bass;
    const auto full = generatePattern(style, request);
    REQUIRE(full.success);
    REQUIRE(full.pattern.voices.size() == static_cast<size_t>(kMaxVoices));
    Pattern pattern = full.pattern;
    CHECK_FALSE(addVoice(pattern, VoiceRole::Stabs, style, request, 1));
    CHECK(pattern == full.pattern);
}

TEST_CASE("the extra voices of the settings are written, read and limited", "[core][roles][settings]") {
    CHECK(formatVoiceRoles({}).empty());
    CHECK(formatVoiceRoles({VoiceRole::Stabs, VoiceRole::Arp, VoiceRole::Counter}) == "stabs,arp,counter");
    CHECK(parseVoiceRoles("stabs,arp,counter") ==
          std::vector<VoiceRole>{VoiceRole::Stabs, VoiceRole::Arp, VoiceRole::Counter});
    CHECK(parseVoiceRoles("").empty());
    CHECK(parseVoiceRoles("arp,drums,,counter") == std::vector<VoiceRole>{VoiceRole::Arp, VoiceRole::Counter});
    CHECK(parseVoiceRoles("arp,arp,arp,arp,arp,arp,arp,arp,arp").size() == static_cast<size_t>(kMaxVoices - 2));

    GenerationSettings settings;
    settings.extraVoices.assign(20, VoiceRole::Arp);
    CHECK(sanitize(settings).extraVoices.size() == static_cast<size_t>(kMaxVoices - 2));
    settings.extraVoices = {VoiceRole::Counter};
    CHECK(sanitize(settings).extraVoices == settings.extraVoices);
}
