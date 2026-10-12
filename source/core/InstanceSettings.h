#pragma once

#include "core/EvolveLayer.h"
#include "core/Pattern.h"
#include "core/Theory.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mm::core {

/// Role of an instance in a project (SPEC 6.5). Only Solo and Hub act in the first step; Voice is stored.
enum class InstanceRole { Solo, Hub, Voice };

/// What the instance sends to its MIDI output (SPEC 6.5): the notes of one voice, or nothing.
enum class OutputMode { OneVoice, None };

/// What a voice does with the slot (SPEC 6.1a, D-131): play the slot of the hub, or follow its own slot parameter.
enum class SlotFollow { Hub, Own };

/// What "Generate" asks for (SPEC 3.1, 3.3 basic parameters). A new instance starts in A minor (SPEC 8.1); an empty
/// key or scale means "auto": drawn for every request (older states, D-149). No seed means a new random seed for every
/// request; a seed makes a request repeat the same pattern.
/// Longest prompt text that is kept (bytes).
inline constexpr size_t kMaxPromptBytes = 2000;

struct GenerationSettings {
    std::string styleId = "peak_time";
    uint32_t lengthBars = 4;                              ///< 1, 2, 4, 8 or 16
    int energyPct = 50;                                   ///< 0 to 100
    int creativityPct = 40;                               ///< 0 to 100
    std::optional<PitchClass> root = PitchClass{9};       ///< 0 to 11 (0 = C), A by default
    std::optional<std::string> scaleId = "natural_minor"; ///< an id of `allScales()`
    std::optional<uint64_t> seed;                         ///< fixed seed, else random per request
    std::string prompt; ///< what the musician asks the AI for (SPEC 3.1); offline: ignored
    std::optional<int> chromaticPct{}; ///< 0 to 100: the chromatic share of SPEC 3.8; none: the style's

    bool operator==(const GenerationSettings&) const = default;
};

/// Brings every field into its valid range: an invalid length becomes 4 bars, percentages are clamped.
GenerationSettings sanitize(GenerationSettings settings);

/// Persistent settings of an instance besides the slots and the host parameters (SPEC 9.1).
struct InstanceSettings {
    InstanceRole role = InstanceRole::Solo;
    OutputMode outputMode = OutputMode::OneVoice;
    int outputVoice = 1; ///< 1 to `kMaxVoices`
    GenerationSettings generation;
    SlotFollow slotFollow = SlotFollow::Hub; ///< only a voice looks at it
    int octave = 0;                          ///< -2 to +2: this instance plays its voice that many octaves up (D-147)
    EvolveSettings evolve{};                 ///< what the Evolve layer does when it is on (SPEC 6.7); on/off is not saved

    bool operator==(const InstanceSettings&) const = default;
};

std::string_view toString(InstanceRole role);
std::string_view toString(OutputMode mode);
std::string_view toString(SlotFollow follow);
/// Unknown text gives the default (Solo, one voice), so that states of later versions still load.
InstanceRole parseRole(std::string_view text);
OutputMode parseOutputMode(std::string_view text);
/// Unknown text gives `Hub`.
SlotFollow parseSlotFollow(std::string_view text);
/// Clamps to 1 to `kMaxVoices`.
int clampOutputVoice(int voice);
/// The seed text of the state and of the seed field: decimal digits. Anything else (empty, sign, letters, overflow)
/// gives no seed, so a bad entry means "random".
std::optional<uint64_t> parseSeed(std::string_view text);
/// Clamps to -2 to +2.
int clampOctave(int octave);
/// The size of the editor in percent of its standard size (SPEC 8.1: 75 to 200 %, the aspect ratio stays). The size box
/// offers the steps, the corner of the window sets any whole percent in between.
inline constexpr int kUiScaleSteps[] = {75, 100, 125, 150, 175, 200};
inline constexpr int kMinUiScale = 75;
inline constexpr int kMaxUiScale = 200;
inline constexpr int kDefaultUiScale = 100;
/// `percent` limited to `kMinUiScale` .. `kMaxUiScale` (a size between two steps stays as it is).
int clampUiScale(int percent);
/// The size after dragging the corner of a window that was `startWidth` wide on screen at `startPercent`, by `dragX`
/// on screen (to the right is larger): the width follows the drag, the height follows the width. Limited like
/// `clampUiScale`; a width of 0 or less leaves the size unchanged.
int uiScaleForDrag(int startPercent, double startWidth, double dragX);

} // namespace mm::core
