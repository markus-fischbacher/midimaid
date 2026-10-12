#include "plugin/AiUsage.h"

#include "MidiMaidConfig.h"
#include "core/TextKeys.h"
#include "plugin/AiConnection.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

namespace {

mm::ai::PricingTable embeddedPricing() {
    int size = 0;
    const char* data = MidiMaidConfig::getNamedResource("pricing_json", size);
    return data != nullptr ? mm::ai::parsePricing(std::string_view(data, static_cast<size_t>(size)))
                           : mm::ai::PricingTable{};
}

} // namespace

AiUsage::AiUsage(juce::File userPricingFile) : userFile_(std::move(userPricingFile)), shipped_(embeddedPricing()) {
    table_ = shipped_;
}

AiUsage& AiUsage::instance() {
    static AiUsage usage(midiMaidDataFolder().getChildFile("pricing.json"));
    return usage;
}

void AiUsage::reloadIfChanged() {
    if (userFile_ == juce::File() || !userFile_.existsAsFile()) {
        if (everLoaded_) { // the file was removed: back to the shipped prices
            table_ = shipped_;
            everLoaded_ = false;
        }
        return;
    }
    const auto modified = userFile_.getLastModificationTime();
    if (everLoaded_ && modified == loaded_) {
        return;
    }
    loaded_ = modified;
    everLoaded_ = true;
    table_ = mm::ai::mergePricing(shipped_, mm::ai::parsePricing(userFile_.loadFileAsString().toStdString()));
}

AiUsage::Entry AiUsage::record(std::string_view model, bool cloud, int inputTokens, int outputTokens) {
    const std::lock_guard lock(mutex_);
    reloadIfChanged();
    Entry entry;
    entry.inputTokens = inputTokens;
    entry.outputTokens = outputTokens;
    if (cloud) {
        entry.cost = mm::ai::estimateCost(table_, model, inputTokens, outputTokens);
    }
    session_ = mm::ai::addUsage(session_, inputTokens, outputTokens, entry.cost);
    return entry;
}

mm::ai::UsageTally AiUsage::session() const {
    const std::lock_guard lock(mutex_);
    return session_;
}

mm::ai::PricingTable AiUsage::pricing() const {
    const std::lock_guard lock(mutex_);
    return table_;
}

void AiUsage::resetSession() {
    const std::lock_guard lock(mutex_);
    session_ = {};
}

std::string usageStatusText(const AiUsage::Entry& last, const mm::ai::UsageTally& session,
                            const std::string& currency) {
    namespace keys = mm::core::text;
    if (last.inputTokens + last.outputTokens <= 0) {
        return {};
    }
    const auto& table = embeddedTranslation();
    const auto lastTokens = mm::ai::formatTokenCount(last.inputTokens + last.outputTokens);
    const auto sessionTokens = mm::ai::formatTokenCount(session.tokens());
    if (!last.cost) { // a local model, or one without a price: the tokens only
        return table.tr(keys::kUsageTokens, {{"tokens", lastTokens}, {"session", sessionTokens}});
    }
    // an incomplete session cost (a request without a price) is marked with "+"
    const auto sessionCost = mm::ai::formatCost(session.cost) + (session.unpriced > 0 ? "+" : "");
    return table.tr(keys::kUsageCost, {{"tokens", lastTokens},
                                       {"cost", mm::ai::formatCost(*last.cost)},
                                       {"currency", currency},
                                       {"session", sessionTokens},
                                       {"sessionCost", sessionCost}});
}

} // namespace mm::plugin
