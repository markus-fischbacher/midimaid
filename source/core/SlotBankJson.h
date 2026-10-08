#pragma once

#include "core/SlotBank.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// Version of the slot bank JSON format. Every change to the format raises it and gets a migration (SPEC 5, 9.1).
constexpr int kSlotBankStateVersion = 1;

/// Serialises all 16 slots (SPEC 9.1): per slot the current pattern, name, colour and the last `historyLimit` results.
/// Keys are sorted, so equal banks give byte-identical text. The same form serves the project state and, later,
/// the library's slot sets.
nlohmann::json slotBankToJson(const SlotBank& bank, size_t historyLimit = kStateHistory);
std::string slotBankToString(const SlotBank& bank, size_t historyLimit = kStateHistory);

struct SlotBankLoadResult {
    std::optional<SlotBank> bank;
    std::string error;                 ///< empty when the document itself could be read
    std::vector<std::string> problems; ///< one entry (with the path) per slot that was left empty
    bool fromNewerVersion = false;     ///< written by a newer plugin version; loaded as far as understood

    bool ok() const { return bank.has_value(); }
};

/// Loads a slot bank. Never throws and never crashes on bad input: unknown fields are ignored, a missing slot stays
/// empty, a slot with bad data stays empty and is reported in `problems` while the others load.
SlotBankLoadResult loadSlotBank(std::string_view text);
SlotBankLoadResult loadSlotBankJson(const nlohmann::json& document);

} // namespace mm::core
