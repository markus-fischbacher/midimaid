#pragma once

#include "ai/Pricing.h"

#include <juce_core/juce_core.h>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace mm::plugin {

/// What the AI requests of this process used (SPEC 7.5): tokens and an estimated cost of the last request and of the
/// session (the process, all instances). The prices are `pricing.json` shipped with the plug-in, overridden by the file
/// of the same name in the data folder (read again when it changes). Thread safe: requests end on worker threads.
class AiUsage {
public:
    /// One request: its tokens and, when the model has a price, its cost.
    struct Entry {
        int inputTokens = 0;
        int outputTokens = 0;
        std::optional<double> cost;
        bool operator==(const Entry&) const = default;
    };

    explicit AiUsage(juce::File userPricingFile);
    static AiUsage& instance();

    /// Adds a request. `cloud` false (a local provider): only the tokens count, there is never a cost.
    Entry record(std::string_view model, bool cloud, int inputTokens, int outputTokens);

    mm::ai::UsageTally session() const;
    mm::ai::PricingTable pricing() const; ///< what is in use now (after the last request)
    void resetSession();

private:
    void reloadIfChanged();

    mutable std::mutex mutex_;
    juce::File userFile_;
    juce::Time loaded_;
    bool everLoaded_ = false;
    mm::ai::PricingTable shipped_;
    mm::ai::PricingTable table_;
    mm::ai::UsageTally session_;
};

/// The text for the status line after an AI request: "Tokens 1,2k · ~0,01 USD (Sitzung: 3,4k · ~0,04 USD)", for a
/// local model without the cost. Empty when the request used no tokens.
std::string usageStatusText(const AiUsage::Entry& last, const mm::ai::UsageTally& session, const std::string& currency);

} // namespace mm::plugin
