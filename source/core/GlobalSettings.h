#pragma once

#include "core/EditGrid.h"
#include "core/Shortcuts.h"
#include "core/Theory.h"
#include "core/Transpose.h"

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
    std::string language = "auto";              ///< the UI language: "auto" (system), "de" or "en" (SPEC 8.2)
    int middleC = 3;                            ///< octave number of MIDI note 60: 3 (Live, Logic) or 4
    bool snapChromatic = false;                 ///< pitch snapping: chromatic instead of to the scale
    bool favoritesToActiveSet = true; ///< a favorite also goes into the active reference set (SPEC 3.20)
    bool exportGroove = true; ///< the MIDI export holds the groove (swing, template), not the grid (SPEC 3.9)
    bool exportAllNotes = false; ///< the MIDI export holds every note, chances and conditions not evaluated (SPEC 3.4)
    bool rollFold = false;    ///< the roll shows only the tones of the scale as rows (SPEC 3.5, D-232)
    bool rollGuides = true;   ///< the roll draws the phrase borders and the kick grid in the background
    bool rollMarkers = false; ///< the roll has a row for the accent and slide marks of the notes
    std::map<std::string, std::string> shortcuts; ///< the keys the user gave to actions: id -> chord (SPEC 8.3, D-235); none by default
    std::string drumMap; ///< drum rack notes as a line "60=kick, 61=closed_hat" (SPEC 3.14); empty: General MIDI only
    TransposeSettings transpose; ///< the keyboard transposition (SPEC 3.16, 8.4): reference, mode, timing, target ...
    AiSettings ai;       ///< provider, models, consent, logging

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
