#include "core/ParameterRegister.h"

namespace mm::core {

namespace {

using enum ParamType;
using enum ParamActive;

// The order of the entries is the order in SPEC 3.13. IDs and names must not be changed (D-93).
constexpr std::array<ParamSpec, kParameterCount> kRegister{{
    {"slot", "Slot", Int, 1, 16, 1, V1_0},
    {"mute_1", "Mute 1", Bool, 0, 1, 0, V1_0},
    {"mute_2", "Mute 2", Bool, 0, 1, 0, V1_0},
    {"mute_3", "Mute 3", Bool, 0, 1, 0, V1_0},
    {"mute_4", "Mute 4", Bool, 0, 1, 0, V1_0},
    {"mute_5", "Mute 5", Bool, 0, 1, 0, V1_0},
    {"mute_6", "Mute 6", Bool, 0, 1, 0, V1_0},
    {"mute_7", "Mute 7", Bool, 0, 1, 0, V1_0},
    {"mute_8", "Mute 8", Bool, 0, 1, 0, V1_0},
    {"transpose", "Transpose", Int, -12, 12, 0, V1_1},
    {"generate", "Generate", Bool, 0, 1, 0, V1_1},
    {"variation_1", "Variation 1", Bool, 0, 1, 0, V1_1},
    {"variation_2", "Variation 2", Bool, 0, 1, 0, V1_1},
    {"variation_3", "Variation 3", Bool, 0, 1, 0, V1_1},
    {"variation_4", "Variation 4", Bool, 0, 1, 0, V1_1},
    {"variation_5", "Variation 5", Bool, 0, 1, 0, V1_1},
    {"variation_6", "Variation 6", Bool, 0, 1, 0, V1_1},
    {"variation_7", "Variation 7", Bool, 0, 1, 0, V1_1},
    {"variation_8", "Variation 8", Bool, 0, 1, 0, V1_1},
    {"variation_all", "Variation All", Bool, 0, 1, 0, V1_1},
    {"evolve", "Evolve", Bool, 0, 1, 0, V1_1},
    {"evolve_keep", "Evolve Keep", Bool, 0, 1, 0, V1_1},
    {"creativity", "Creativity", Int, 0, 100, 40, Backlog},
    {"energy", "Energy", Int, 0, 100, 50, Backlog},
}};

} // namespace

std::span<const ParamSpec> parameterRegister() {
    return kRegister;
}

const ParamSpec* findParameter(std::string_view id) {
    for (const ParamSpec& spec : kRegister) {
        if (spec.id == id) {
            return &spec;
        }
    }
    return nullptr;
}

std::string muteParameterId(int voice) {
    if (voice < 1 || voice > kParameterVoices) {
        return {};
    }
    return "mute_" + std::to_string(voice);
}

} // namespace mm::core
