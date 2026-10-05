#pragma once

#include "core/Pattern.h"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mm::core {

/// Version of the pattern JSON format. Every change to the format raises it and gets a migration (SPEC 5).
constexpr int kPatternStateVersion = 1;

/// Serialises a pattern. Keys are sorted, so equal patterns give byte-identical text. Fields for v1.1 are written
/// with their defaults.
nlohmann::json patternToJson(const Pattern& pattern);
std::string patternToString(const Pattern& pattern);

struct LoadResult {
    std::optional<Pattern> pattern;
    std::string error;             ///< empty on success; otherwise path and reason
    bool fromNewerVersion = false; ///< written by a newer plugin version; loaded as far as understood

    bool ok() const { return pattern.has_value(); }
};

/// Loads and validates a pattern. Never throws and never crashes on bad input: unknown fields are ignored, malformed
/// or invalid data gives an error with the JSON path instead of a silent repair.
LoadResult loadPattern(std::string_view text);
LoadResult loadPatternJson(const nlohmann::json& document);

/// One migration step upgrades a document by exactly one version (steps[0]: 1 -> 2, steps[1]: 2 -> 3, ...).
using Migration = std::function<bool(nlohmann::json& document, std::string& error)>;

/// Applies the steps from `fromVersion` up to `toVersion` and sets `stateVersion`. Fails for versions below 1 or when
/// steps are missing.
bool migrateDocument(nlohmann::json& document, int fromVersion, int toVersion, std::span<const Migration> steps,
                     std::string& error);

} // namespace mm::core
