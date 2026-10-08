#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace mm::core {

/// The host parameter register (SPEC 3.13, D-93, D-138). The IDs are fixed strings: they appear in DAW projects and
/// automation, so they never change once the plugin is in use. The register has no JUCE dependency so that a test can
/// pin the IDs.

/// `versionHint` of every parameter created from the register (needed by AU, SPEC 3.13).
constexpr int kParameterVersionHint = 1;

enum class ParamType { Int, Bool };

/// The release from which a parameter has an effect. Parameters of later releases already exist with their final ID
/// and name but do nothing and are marked as not automatable.
enum class ParamActive { V1_0, V1_1, Backlog };

struct ParamSpec {
    std::string_view id;
    std::string_view name; ///< host-visible, language independent (D-138)
    ParamType type;
    int min;
    int max;
    int defaultValue;
    ParamActive active;

    bool isActive() const { return active == ParamActive::V1_0; }
};

constexpr size_t kParameterCount = 24;
/// Voices with a mute and a variation parameter (SPEC 3.13).
constexpr int kParameterVoices = 8;

/// All parameters in register order.
std::span<const ParamSpec> parameterRegister();
/// The parameter with this ID, or nullptr.
const ParamSpec* findParameter(std::string_view id);
/// `mute_1` ... `mute_8` for voice 1 to 8; empty for other numbers.
std::string muteParameterId(int voice);

} // namespace mm::core
