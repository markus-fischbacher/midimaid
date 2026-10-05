#pragma once

#include "core/Register.h"
#include "core/Theory.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

struct WeightedId {
    std::string id;
    int weight = 1;

    bool operator==(const WeightedId&) const = default;
};

/// How the bass chooses its pitches, in percent (the three values add up to 100).
struct BassMovement {
    int rootPercent = 70;
    int fifthOctavePercent = 25;
    int passingPercent = 5;
    bool rootOnChordChange = false; ///< Melodic Techno: the root sounds on every chord change

    bool operator==(const BassMovement&) const = default;
};

struct BassProfile {
    Range range = kDefaultBassRange;
    uint32_t kickClearanceTicks = 120; ///< "off" 0, "1/64" 60, "1/32" 120, "1/16" 240
    float swingDefault = 0.5f;
    BassMovement movement;
    std::vector<WeightedId> archetypes;

    bool operator==(const BassProfile&) const = default;
};

struct MelodyProfile {
    Range range = kDefaultMelodyRange;
    Range voicingRange = kDefaultStabRange;
    bool chordMemory = false; ///< [v1.1] field, read but not used in v1.0
    float swingDefault = 0.5f;
    std::vector<WeightedId> archetypes; ///< ids without an implementation are skipped when an archetype is chosen
    std::vector<WeightedId> chordColors;

    bool operator==(const MelodyProfile&) const = default;
};

struct HarmonyProfile {
    std::vector<WeightedId> modes;                ///< static, two_chord, four_chord
    std::vector<std::vector<Chord>> progressions; ///< 2-4 chords each
    int chordLengthBarsMin = 4;
    int chordLengthBarsMax = 8;

    bool operator==(const HarmonyProfile&) const = default;
};

/// Weights of the soft criteria and the minimum score of the quality rating (SPEC 4.4, STYLES.md 1.14).
struct QualityProfile {
    int minScore = 50;
    int motif = 3;
    int range = 1;
    int density = 2;
    int kick = 2;
    int intervals = 2;
    int rhythmVariety = 1;

    bool operator==(const QualityProfile&) const = default;
};

/// A style profile (STYLES.md 2-5). Weights and shares are whole numbers so that the generators calculate with
/// integers.
struct StyleProfile {
    std::string id;
    uint32_t version = 1; ///< raised with every change to the profile values (`GenerationInfo::styleProfileVersion`)
    std::string nameDe;
    std::string nameEn;
    int tempoMin = 120;
    int tempoMax = 130;
    std::vector<WeightedId> scales;
    int chromaticDefaultPercent = 0;
    HarmonyProfile harmony;
    std::string kickDefault = "4otf";
    std::vector<std::string> kickVariants;
    BassProfile bass;
    MelodyProfile melody;
    QualityProfile quality;

    /// The voice ranges of this profile for `effectiveRange` and the constraint settings.
    RegisterProfile registerProfile() const { return {bass.range, melody.range}; }

    bool operator==(const StyleProfile&) const = default;
};

/// Checks a profile: known scales, valid progressions, narrowed (never widened) ranges of at least 12 pitches, known
/// kick grids, shares adding up to 100 and so on. One message per violation, starting with the field path.
std::vector<std::string> validateStyleProfile(const StyleProfile& profile);

struct StyleLoadResult {
    std::optional<StyleProfile> profile;
    std::string error; ///< empty on success; otherwise path and reason

    bool ok() const { return profile.has_value(); }
};

/// Loads and validates a profile (JSON format of STYLES.md 5). Never throws; unknown fields are ignored so that fields
/// of later versions stay readable.
StyleLoadResult loadStyleProfile(std::string_view text);
StyleLoadResult loadStyleProfileJson(const nlohmann::json& document);

} // namespace mm::core
