#pragma once

#include "core/EditGrid.h"
#include "core/Theory.h"

#include <string>
#include <string_view>

namespace mm::core {

/// Version of the settings file; a file of a later version loads as far as it is understood.
constexpr int kGlobalSettingsVersion = 1;

/// Settings of the user that are not part of a project: they apply to every instance and window (SPEC 8.1, 8.4, D-162).
/// No JUCE: the plugin reads and writes the file in the background and hands the text to these functions.
struct GlobalSettings {
    bool expert = false;                        ///< the expert level of the hub UI is shown
    PitchClass startRoot = 9;                   ///< the key a new instance starts with: the last one chosen, A first
    std::string startScaleId = "natural_minor"; ///< and its scale
    int variationStrength = 30;                 ///< 0 to 100: the strength "Variation" uses
    EditGrid grid;                              ///< the edit grid of the piano roll
    bool snapChromatic = false;                 ///< pitch snapping: chromatic instead of to the scale

    bool operator==(const GlobalSettings&) const = default;
};

/// Brings every field into its valid range (an unknown scale or division gives the default, strength is clamped).
GlobalSettings sanitize(GlobalSettings settings);

/// Reads the file text. Never fails: unreadable text, a wrong type, a missing or invalid version (below 1) gives the
/// defaults, a single bad field gives the default of that field, unknown fields are ignored.
GlobalSettings parseGlobalSettings(std::string_view text);

/// The text of the file for `settings` (sanitized first), pretty-printed JSON with the current version.
std::string toJson(const GlobalSettings& settings);

} // namespace mm::core
