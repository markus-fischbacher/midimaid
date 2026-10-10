#pragma once

#include "core/EditGrid.h"
#include "core/Theory.h"

#include <map>
#include <set>
#include <string>
#include <string_view>

namespace mm::core {

/// Version of the settings file; a file of a later version loads as far as it is understood.
constexpr int kGlobalSettingsVersion = 1;

/// What the musician chose for one AI provider (SPEC 8.4). Empty or zero means "the default of `models.json`". The API
/// key is not here: it lives in the keychain of the operating system only (SPEC 8.5).
struct AiProviderSettings {
    std::string model;
    std::string baseUrl;     ///< own address (`custom`) or another address for a preset; empty: the preset's
    int timeoutSeconds = 0;  ///< 0: the provider's default (cloud 30 s, Ollama 120 s)
    int maxTokens = 0;       ///< 0: the default (4096)
    std::string schemaLevel; ///< expert: "enforced", "json" or "prompt"; empty: `models.json`

    bool operator==(const AiProviderSettings&) const = default;
};

constexpr int kDefaultMaxTokens = 4096;

/// The AI part of the settings (SPEC 7, 8.4, 10).
struct AiSettings {
    std::string activeProvider; ///< id from `models.json`; empty: no provider, everything runs offline (SPEC 7.5)
    std::map<std::string, AiProviderSettings> providers;
    bool logPrompts = false;            ///< "Prompts protokollieren" (SPEC 10): prompts and answers go to the log
    std::set<std::string> cloudConsent; ///< providers that may get references and imported voices (SPEC 3.20, D-86)

    bool operator==(const AiSettings&) const = default;
};

/// Settings of the user that are not part of a project: they apply to every instance and window (SPEC 8.1, 8.4, D-162).
/// No JUCE: the plugin reads and writes the file in the background and hands the text to these functions.
struct GlobalSettings {
    bool expert = false;                        ///< the expert level of the hub UI is shown
    PitchClass startRoot = 9;                   ///< the key a new instance starts with: the last one chosen, A first
    std::string startScaleId = "natural_minor"; ///< and its scale
    int variationStrength = 30;                 ///< 0 to 100: the strength "Variation" uses
    EditGrid grid;                              ///< the edit grid of the piano roll
    bool snapChromatic = false;                 ///< pitch snapping: chromatic instead of to the scale
    AiSettings ai;                              ///< provider, models, consent, logging

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
