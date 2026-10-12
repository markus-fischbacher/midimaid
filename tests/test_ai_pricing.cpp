#include "ai/Pricing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <sstream>

using namespace mm::ai;
using Catch::Approx;

namespace {

PricingTable sample() {
    return parsePricing(R"({"version": 1, "currency": "EUR", "models": [
        {"model": "big", "inputPerMillion": 10, "outputPerMillion": 50},
        {"model": "big-mini", "inputPerMillion": 1, "outputPerMillion": 4}]})");
}

} // namespace

TEST_CASE("the pricing file is read, bad entries are left out", "[pricing]") {
    const auto table = sample();
    CHECK(table.currency == "EUR");
    REQUIRE(table.models.size() == 2);
    CHECK(table.models[0].model == "big");
    CHECK(table.models[0].inputPerMillion == Approx(10.0));
    CHECK(table.models[0].outputPerMillion == Approx(50.0));

    const auto bad = parsePricing(R"({"models": [
        {"model": "", "inputPerMillion": 1, "outputPerMillion": 1},
        {"inputPerMillion": 1, "outputPerMillion": 1},
        {"model": "neg", "inputPerMillion": -1, "outputPerMillion": 1},
        {"model": "text", "inputPerMillion": "1", "outputPerMillion": 1},
        {"model": "missing", "inputPerMillion": 1},
        {"model": "huge", "inputPerMillion": 1e9, "outputPerMillion": 1},
        "x",
        {"model": "ok", "inputPerMillion": 0, "outputPerMillion": 0}]})");
    REQUIRE(bad.models.size() == 1);
    CHECK(bad.models[0].model == "ok");
    CHECK(bad.currency == "USD");
}

TEST_CASE("unreadable pricing text gives an empty table", "[pricing]") {
    for (const char* text : {"", "not json", "[]", "42", "{", "{\"models\": 3}"}) {
        INFO(text);
        CHECK(parsePricing(text).models.empty());
    }
}

TEST_CASE("the shipped pricing file is valid", "[pricing]") {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/config/pricing.json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto table = parsePricing(buffer.str());
    CHECK(table.currency == "USD");
    CHECK(table.models.size() >= 6);
    CHECK(estimateCost(table, "claude-sonnet-5-5", 1000, 1000).has_value());
    CHECK(estimateCost(table, "gpt-4o-mini-2024-07-18", 1000, 1000).has_value());
    CHECK_FALSE(estimateCost(table, "qwen3-27b", 1000, 1000).has_value());
}

TEST_CASE("the cost is tokens times the price per million", "[pricing]") {
    const auto table = sample();
    CHECK(estimateCost(table, "big", 1000000, 0).value() == Approx(10.0));
    CHECK(estimateCost(table, "big", 0, 1000000).value() == Approx(50.0));
    CHECK(estimateCost(table, "big", 2000, 500).value() == Approx(0.02 + 0.025));
    CHECK(estimateCost(table, "big", -5, -5).value() == Approx(0.0));
}

TEST_CASE("the longest matching model id wins and an unknown model has no cost", "[pricing]") {
    const auto table = sample();
    CHECK(estimateCost(table, "big-mini", 1000000, 0).value() == Approx(1.0));
    CHECK(estimateCost(table, "big-mini-2025", 1000000, 0).value() == Approx(1.0));
    CHECK(estimateCost(table, "big-2025", 1000000, 0).value() == Approx(10.0));
    CHECK_FALSE(estimateCost(table, "bi", 1, 1).has_value());
    CHECK_FALSE(estimateCost(table, "", 1, 1).has_value());
    CHECK_FALSE(estimateCost(PricingTable{}, "big", 1, 1).has_value());
}

TEST_CASE("the file of the user replaces and adds prices", "[pricing]") {
    const auto user = parsePricing(R"({"currency": "EUR", "models": [
        {"model": "big", "inputPerMillion": 1, "outputPerMillion": 2},
        {"model": "other", "inputPerMillion": 3, "outputPerMillion": 4}]})");
    const auto merged = mergePricing(sample(), user);
    CHECK(merged.currency == "EUR");
    REQUIRE(merged.models.size() == 3);
    CHECK(estimateCost(merged, "big", 1000000, 0).value() == Approx(1.0));
    CHECK(estimateCost(merged, "big-mini", 1000000, 0).value() == Approx(1.0));
    CHECK(estimateCost(merged, "other", 1000000, 0).value() == Approx(3.0));

    const auto kept = mergePricing(sample(), PricingTable{});
    CHECK(kept.currency == "EUR");
    CHECK(kept.models.size() == 2);
}

TEST_CASE("the usage of requests adds up and counts the unpriced ones", "[pricing]") {
    UsageTally tally;
    tally = addUsage(tally, 1000, 200, 0.5);
    tally = addUsage(tally, 300, 100, std::nullopt);
    tally = addUsage(tally, -4, -4, 0.25);
    CHECK(tally.requests == 3);
    CHECK(tally.inputTokens == 1300);
    CHECK(tally.outputTokens == 300);
    CHECK(tally.tokens() == 1600);
    CHECK(tally.cost == Approx(0.75));
    CHECK(tally.unpriced == 1);
}

TEST_CASE("token counts and amounts are short for the status line", "[pricing]") {
    CHECK(formatTokenCount(0) == "0");
    CHECK(formatTokenCount(950) == "950");
    CHECK(formatTokenCount(999) == "999");
    CHECK(formatTokenCount(1000) == "1k");
    CHECK(formatTokenCount(1234) == "1,2k");
    CHECK(formatTokenCount(9999) == "10k");
    CHECK(formatTokenCount(12345) == "12k");
    CHECK(formatTokenCount(999999) == "1000k");
    CHECK(formatTokenCount(1400000) == "1,4M");
    CHECK(formatTokenCount(-5) == "0");
    CHECK(formatCost(0.004) == "0,004");
    CHECK(formatCost(0.01) == "0,01");
    CHECK(formatCost(1.25) == "1,25");
    CHECK(formatCost(0.0) == "0,00");
    CHECK(formatCost(12.0) == "12,00");
}
