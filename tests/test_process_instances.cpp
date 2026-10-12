#include "platform/ProcessInstances.h"

#include <catch2/catch_test_macros.hpp>

using namespace mm::platform;

TEST_CASE("instance counts are kept per tag", "[process-instances]") {
    std::string state;
    state = withInstanceCount(state, "au", 1);
    state = withInstanceCount(state, "au", 1);
    state = withInstanceCount(state, "vst3", 1);
    CHECK(state == "au=2;vst3=1");
    CHECK(instanceCount(state, "au") == 2);
    CHECK(instanceCount(state, "vst3") == 1);
    CHECK(instanceCount(state, "clap") == 0);
    state = withInstanceCount(state, "au", -1);
    CHECK(instanceCount(state, "au") == 1);
}

TEST_CASE("a count never goes below zero and empty tags disappear", "[process-instances]") {
    auto state = withInstanceCount("", "au", -1);
    CHECK(state.empty());
    state = withInstanceCount(withInstanceCount("", "au", 1), "au", -1);
    CHECK(state.empty());
    CHECK(withInstanceCount("au=1;vst3=1", "au", -5) == "vst3=1");
}

TEST_CASE("other formats are found only when they have instances", "[process-instances]") {
    CHECK_FALSE(otherTagsPresent("", "au"));
    CHECK_FALSE(otherTagsPresent("au=3", "au"));
    CHECK(otherTagsPresent("au=3;vst3=1", "au"));
    CHECK(otherTagsPresent("au=3;vst3=1", "vst3"));
    CHECK_FALSE(otherTagsPresent("au=3;vst3=0", "au"));
}

TEST_CASE("a damaged state is read as far as it is valid", "[process-instances]") {
    CHECK(instanceCount("garbage;au=2;=4;vst3=x;clap=", "au") == 2);
    CHECK(instanceCount("garbage;au=2;=4;vst3=x;clap=", "vst3") == 0);
    CHECK(instanceCount("au=99999999999", "au") == 0);
}

TEST_CASE("the process environment counts the instances of a registration", "[process-instances]") {
    CHECK_FALSE(otherFormatsPresent("zz_a"));
    {
        InstanceRegistration first("zz_a");
        CHECK_FALSE(otherFormatsPresent("zz_a"));
        CHECK(otherFormatsPresent("zz_b"));
        {
            InstanceRegistration second("zz_b");
            CHECK(otherFormatsPresent("zz_a"));
        }
        CHECK_FALSE(otherFormatsPresent("zz_a"));
    }
    CHECK_FALSE(otherFormatsPresent("zz_b"));
    InstanceRegistration none(""); // an empty tag registers nothing
    CHECK_FALSE(otherFormatsPresent("zz_a"));
}
