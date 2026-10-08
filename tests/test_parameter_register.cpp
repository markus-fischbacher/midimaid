#include "core/ParameterRegister.h"

#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string>
#include <vector>

using namespace mm::core;

TEST_CASE("the parameter IDs are fixed (D-93)", "[parameters]") {
    // These IDs are in DAW projects and automation: never change them. Adding one needs a new decision.
    const std::vector<std::string> expected{"slot",        "mute_1",      "mute_2",      "mute_3",      "mute_4",
                                            "mute_5",      "mute_6",      "mute_7",      "mute_8",      "transpose",
                                            "generate",    "variation_1", "variation_2", "variation_3", "variation_4",
                                            "variation_5", "variation_6", "variation_7", "variation_8", "variation_all",
                                            "evolve",      "evolve_keep", "creativity",  "energy"};
    const auto parameters = parameterRegister();
    REQUIRE(parameters.size() == expected.size());
    REQUIRE(parameters.size() == kParameterCount);
    for (size_t i = 0; i < expected.size(); ++i) {
        CHECK(std::string(parameters[i].id) == expected[i]);
    }
}

TEST_CASE("IDs and names are unique and not empty", "[parameters]") {
    std::set<std::string> ids;
    std::set<std::string> names;
    for (const ParamSpec& spec : parameterRegister()) {
        CHECK_FALSE(spec.id.empty());
        CHECK_FALSE(spec.name.empty());
        ids.insert(std::string(spec.id));
        names.insert(std::string(spec.name));
    }
    CHECK(ids.size() == kParameterCount);
    CHECK(names.size() == kParameterCount);
}

TEST_CASE("ranges, defaults and types follow SPEC 3.13", "[parameters]") {
    const ParamSpec* slot = findParameter("slot");
    REQUIRE(slot != nullptr);
    CHECK(slot->type == ParamType::Int);
    CHECK(slot->min == 1);
    CHECK(slot->max == 16);
    CHECK(slot->defaultValue == 1);

    const ParamSpec* transpose = findParameter("transpose");
    REQUIRE(transpose != nullptr);
    CHECK(transpose->min == -12);
    CHECK(transpose->max == 12);
    CHECK(transpose->defaultValue == 0);

    CHECK(findParameter("creativity")->defaultValue == 40);
    CHECK(findParameter("creativity")->max == 100);
    CHECK(findParameter("energy")->defaultValue == 50);

    for (const ParamSpec& spec : parameterRegister()) {
        CHECK(spec.min <= spec.defaultValue);
        CHECK(spec.defaultValue <= spec.max);
        if (spec.type == ParamType::Bool) {
            CHECK(spec.min == 0);
            CHECK(spec.max == 1);
            CHECK(spec.defaultValue == 0);
        }
    }
}

TEST_CASE("only slot and the eight mutes are active in v1.0", "[parameters]") {
    size_t active = 0;
    for (const ParamSpec& spec : parameterRegister()) {
        const std::string id(spec.id);
        const bool expectedActive = id == "slot" || id.rfind("mute_", 0) == 0;
        CHECK(spec.isActive() == expectedActive);
        active += spec.isActive() ? 1 : 0;
    }
    CHECK(active == 9);
    CHECK(findParameter("transpose")->active == ParamActive::V1_1);
    CHECK(findParameter("evolve_keep")->active == ParamActive::V1_1);
    CHECK(findParameter("creativity")->active == ParamActive::Backlog);
}

TEST_CASE("lookup and the mute IDs", "[parameters]") {
    CHECK(findParameter("nope") == nullptr);
    CHECK(findParameter("") == nullptr);
    CHECK(findParameter("mute_8") != nullptr);
    CHECK(findParameter("mute_9") == nullptr);
    CHECK(muteParameterId(1) == "mute_1");
    CHECK(muteParameterId(8) == "mute_8");
    CHECK(muteParameterId(0).empty());
    CHECK(muteParameterId(9).empty());
    for (int voice = 1; voice <= kParameterVoices; ++voice) {
        CHECK(findParameter(muteParameterId(voice)) != nullptr);
    }
    CHECK(kParameterVersionHint == 1);
}
