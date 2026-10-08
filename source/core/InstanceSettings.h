#pragma once

#include "core/Pattern.h"

#include <string_view>

namespace mm::core {

/// Role of an instance in a project (SPEC 6.5). Only Solo and Hub act in the first step; Voice is stored.
enum class InstanceRole { Solo, Hub, Voice };

/// What the instance sends to its MIDI output (SPEC 6.5): the notes of one voice, or nothing.
enum class OutputMode { OneVoice, None };

/// Persistent settings of an instance besides the slots and the host parameters (SPEC 9.1).
struct InstanceSettings {
    InstanceRole role = InstanceRole::Solo;
    OutputMode outputMode = OutputMode::OneVoice;
    int outputVoice = 1; ///< 1 to `kMaxVoices`

    bool operator==(const InstanceSettings&) const = default;
};

std::string_view toString(InstanceRole role);
std::string_view toString(OutputMode mode);
/// Unknown text gives the default (Solo, one voice), so that states of later versions still load.
InstanceRole parseRole(std::string_view text);
OutputMode parseOutputMode(std::string_view text);
/// Clamps to 1 to `kMaxVoices`.
int clampOutputVoice(int voice);

} // namespace mm::core
