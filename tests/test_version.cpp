#include "core/version.h"

#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("core reports a version", "[core]") {
    REQUIRE(std::string(mm::core::version()) == "0.0.1");
}
