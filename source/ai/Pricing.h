#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::ai {

/// The price of one model in `pricing.json` (SPEC 7.5): per million tokens, in the currency of the file.
struct ModelPrice {
    std::string model; ///< the model id; an id that is the start of the used one (a dated variant) matches, too
    double inputPerMillion = 0.0;
    double outputPerMillion = 0.0;
};

struct PricingTable {
    std::string currency = "USD";
    std::vector<ModelPrice> models;
};

/// Reads `pricing.json`. Never fails: unreadable text gives an empty table, a bad entry (no model, a negative or
/// missing price) is left out.
PricingTable parsePricing(std::string_view text);

/// The shipped table with the entries of the user's file on top: the same model id replaces the shipped price, others
/// are added; the currency is the user's when the file names one.
PricingTable mergePricing(PricingTable shipped, const PricingTable& user);

/// The estimated cost of a request, or nothing when the model has no price (a local model costs nothing to show:
/// only the tokens are shown then). The longest matching model id wins.
std::optional<double> estimateCost(const PricingTable& table, std::string_view model, int inputTokens, int outputTokens);

/// What the requests of a period used. `unpriced` counts the requests without a price: the cost is then incomplete.
struct UsageTally {
    int requests = 0;
    long long inputTokens = 0;
    long long outputTokens = 0;
    double cost = 0.0;
    int unpriced = 0;

    long long tokens() const { return inputTokens + outputTokens; }
    bool operator==(const UsageTally&) const = default;
};

UsageTally addUsage(UsageTally tally, int inputTokens, int outputTokens, std::optional<double> cost);

/// "950", "1,2k", "12k", "1,4M": the count for the status line (a decimal comma, like the German UI).
std::string formatTokenCount(long long tokens);
/// "0,004", "0,01", "1,25": the amount of the estimate; small amounts get a third decimal.
std::string formatCost(double amount);

} // namespace mm::ai
