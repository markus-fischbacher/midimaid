#include "ai/Pricing.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace mm::ai {

PricingTable parsePricing(std::string_view text) {
    PricingTable table;
    const auto root = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (!root.is_object()) {
        return table;
    }
    if (const auto it = root.find("currency"); it != root.end() && it->is_string() && !it->get<std::string>().empty()) {
        table.currency = it->get<std::string>();
    }
    const auto models = root.find("models");
    if (models == root.end() || !models->is_array()) {
        return table;
    }
    for (const auto& node : *models) {
        if (!node.is_object()) {
            continue;
        }
        const auto model = node.find("model");
        const auto in = node.find("inputPerMillion");
        const auto out = node.find("outputPerMillion");
        if (model == node.end() || !model->is_string() || model->get<std::string>().empty() || in == node.end() ||
            !in->is_number() || out == node.end() || !out->is_number()) {
            continue;
        }
        const double input = in->get<double>();
        const double output = out->get<double>();
        if (!(input >= 0.0) || !(output >= 0.0) || input > 1.0e6 || output > 1.0e6) {
            continue;
        }
        table.models.push_back({model->get<std::string>(), input, output});
    }
    return table;
}

PricingTable mergePricing(PricingTable shipped, const PricingTable& user) {
    for (const auto& price : user.models) {
        const auto it = std::find_if(shipped.models.begin(), shipped.models.end(),
                                     [&](const ModelPrice& own) { return own.model == price.model; });
        if (it != shipped.models.end()) {
            *it = price;
        } else {
            shipped.models.push_back(price);
        }
    }
    if (!user.models.empty() || user.currency != PricingTable{}.currency) {
        shipped.currency = user.currency;
    }
    return shipped;
}

std::optional<double> estimateCost(const PricingTable& table, std::string_view model, int inputTokens,
                                   int outputTokens) {
    const ModelPrice* best = nullptr;
    for (const auto& price : table.models) {
        if (model.substr(0, price.model.size()) == price.model && (best == nullptr || price.model.size() > best->model.size())) {
            best = &price;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return (static_cast<double>(std::max(inputTokens, 0)) * best->inputPerMillion +
            static_cast<double>(std::max(outputTokens, 0)) * best->outputPerMillion) /
           1.0e6;
}

UsageTally addUsage(UsageTally tally, int inputTokens, int outputTokens, std::optional<double> cost) {
    ++tally.requests;
    tally.inputTokens += std::max(inputTokens, 0);
    tally.outputTokens += std::max(outputTokens, 0);
    if (cost) {
        tally.cost += *cost;
    } else {
        ++tally.unpriced;
    }
    return tally;
}

namespace {

std::string withComma(double value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    std::string text = buffer;
    std::replace(text.begin(), text.end(), '.', ',');
    return text;
}

} // namespace

std::string formatTokenCount(long long tokens) {
    if (tokens < 1000) {
        return std::to_string(std::max(tokens, 0LL));
    }
    const bool million = tokens >= 1000000;
    const double value = static_cast<double>(tokens) / (million ? 1.0e6 : 1.0e3);
    // one decimal below 10, none above; a trailing ",0" is left out
    auto text = withComma(value, value < 10.0 ? 1 : 0);
    if (text.size() > 2 && text.compare(text.size() - 2, 2, ",0") == 0) {
        text.resize(text.size() - 2);
    }
    return text + (million ? "M" : "k");
}

std::string formatCost(double amount) {
    return withComma(std::max(amount, 0.0), amount < 0.01 && amount > 0.0 ? 3 : 2);
}

} // namespace mm::ai
