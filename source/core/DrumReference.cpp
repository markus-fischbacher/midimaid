#include "core/DrumReference.h"

#include "core/Archetype.h"
#include "core/Constraints.h"
#include "core/Groove.h"
#include "core/OutputStage.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace mm::core {

namespace {

constexpr uint32_t kStepTicks = 240;
constexpr int kMaxOffsetTicks = 120; ///< half a 16th: a start further off belongs to the next step

constexpr std::array<std::pair<std::string_view, DrumRole>, 7> kRoleNames{{{"kick", DrumRole::Kick},
                                                                           {"snare", DrumRole::Snare},
                                                                           {"clap", DrumRole::Clap},
                                                                           {"closed_hat", DrumRole::ClosedHat},
                                                                           {"open_hat", DrumRole::OpenHat},
                                                                           {"ride", DrumRole::Ride},
                                                                           {"other", DrumRole::Other}}};

bool isHat(DrumRole role) {
    return role == DrumRole::ClosedHat || role == DrumRole::OpenHat || role == DrumRole::Ride;
}

} // namespace

std::string_view toString(DrumRole role) {
    for (const auto& [name, each] : kRoleNames) {
        if (each == role) {
            return name;
        }
    }
    return "other";
}

std::optional<DrumRole> drumRoleFromString(std::string_view text) {
    for (const auto& [name, each] : kRoleNames) {
        if (name == text) {
            return each;
        }
    }
    return std::nullopt;
}

DrumRole DrumMapping::roleOf(uint8_t note) const {
    for (const Entry& entry : overrides) {
        if (entry.note == note) {
            return entry.role;
        }
    }
    switch (note) {
    case 36:
        return DrumRole::Kick;
    case 38:
        return DrumRole::Snare;
    case 39:
        return DrumRole::Clap;
    case 42:
        return DrumRole::ClosedHat;
    case 46:
        return DrumRole::OpenHat;
    case 51:
        return DrumRole::Ride;
    default:
        return DrumRole::Other;
    }
}

std::string drumMappingToText(const DrumMapping& mapping) {
    std::string text;
    for (const auto& entry : mapping.overrides) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::to_string(entry.note) + "=" + std::string(toString(entry.role));
    }
    return text;
}

DrumMapping drumMappingFromText(std::string_view text) {
    DrumMapping mapping;
    size_t pos = 0;
    const auto trim = [](std::string_view s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
            s.remove_prefix(1);
        }
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
            s.remove_suffix(1);
        }
        return s;
    };
    while (pos <= text.size()) {
        size_t end = text.find_first_of(",;\n", pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view part = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t equals = part.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view number = trim(part.substr(0, equals));
        const auto role = drumRoleFromString(trim(part.substr(equals + 1)));
        if (!role || number.empty() || number.size() > 3 ||
            !std::all_of(number.begin(), number.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            continue;
        }
        const int note = std::atoi(std::string(number).c_str());
        if (note > 127 ||
            std::any_of(mapping.overrides.begin(), mapping.overrides.end(),
                        [note](const DrumMapping::Entry& e) { return e.note == note; })) {
            continue;
        }
        mapping.overrides.push_back({static_cast<uint8_t>(note), *role});
    }
    return mapping;
}

std::optional<RhythmReference> deriveRhythmReference(const std::vector<MidiClipNote>& notes, uint32_t lengthBars,
                                                     const DrumMapping& mapping) {
    if (lengthBars < 1) {
        return std::nullopt;
    }
    const uint32_t bars = lengthBars == 1 ? 1 : 2;
    const uint32_t slots = bars * 16;
    const uint32_t totalSteps = lengthBars * 16;
    const uint32_t repeats = (totalSteps + slots - 1) / slots;

    // per repeat: which slot holds a kick, a hat, an open hat
    std::vector<std::bitset<32>> kicks(repeats);
    std::vector<std::bitset<32>> hats(repeats);
    std::vector<std::bitset<32>> opens(repeats);
    std::array<long long, 32> offsetSum{};
    std::array<long long, 32> velocitySum{};
    std::array<int, 32> hatCount{};
    bool anyKick = false;
    bool anyHat = false;
    for (const MidiClipNote& note : notes) {
        const DrumRole role = mapping.roleOf(note.pitch);
        if (role != DrumRole::Kick && !isHat(role)) {
            continue;
        }
        const uint32_t step = (note.startTick + kStepTicks / 2) / kStepTicks;
        if (step >= totalSteps) {
            continue;
        }
        const uint32_t repeat = step / slots;
        const uint32_t slot = step % slots;
        if (role == DrumRole::Kick) {
            kicks[repeat].set(slot);
            anyKick = true;
            continue;
        }
        hats[repeat].set(slot);
        anyHat = true;
        if (role == DrumRole::OpenHat) {
            opens[repeat].set(slot);
        }
        const int offset = static_cast<int>(note.startTick) - static_cast<int>(step * kStepTicks);
        offsetSum[slot] += std::clamp(offset, -kMaxOffsetTicks, kMaxOffsetTicks);
        velocitySum[slot] += note.velocity;
        ++hatCount[slot];
    }
    if (!anyKick && !anyHat) {
        return std::nullopt;
    }

    const auto majority = [repeats](const std::vector<std::bitset<32>>& per, uint32_t slot) {
        uint32_t seen = 0;
        for (const auto& bits : per) {
            seen += bits.test(slot) ? 1 : 0;
        }
        return seen * 2 >= repeats && seen > 0;
    };
    RhythmReference ref;
    ref.bars = static_cast<uint8_t>(bars);
    long long allVelocity = 0;
    long long allCount = 0;
    for (uint32_t slot = 0; slot < slots; ++slot) {
        if (majority(kicks, slot)) {
            ref.kickSteps.set(slot);
        }
        if (majority(hats, slot)) {
            ref.hatSteps.set(slot);
            const long long n = hatCount[slot];
            const long long offset = offsetSum[slot];
            ref.timingOffsetTicks[slot] =
                static_cast<int16_t>((std::abs(offset) + n / 2) / n * (offset < 0 ? -1 : 1));
            ref.velocity[slot] = static_cast<uint8_t>(std::clamp<long long>((velocitySum[slot] + n / 2) / n, 1, 127));
            allVelocity += ref.velocity[slot];
            ++allCount;
        }
    }
    const long long mean = allCount > 0 ? allVelocity / allCount : 0;
    for (uint32_t slot = 0; slot < slots; ++slot) {
        if (!ref.hatSteps.test(slot)) {
            continue;
        }
        // an open hat is an accent; so is a hat that is clearly louder than the average one
        if (majority(opens, slot) || ref.velocity[slot] * 100 >= mean * 115) {
            ref.accentSteps.set(slot);
        }
    }
    return ref;
}

namespace {

/// A voice with an imported line (locked, no archetype) plays as it was written: groove stays off.
bool playsAsWritten(const Track& track) {
    return isVoiceLocked(track) && track.archetypeId.empty();
}

} // namespace

void applyRhythmReference(Pattern& pattern, const RhythmReference& reference, const StyleProfile& style) {
    pattern.rhythmRef = reference;
    pattern.kickGridId = "custom";
    for (Track& track : pattern.voices) {
        if (!playsAsWritten(track)) {
            track.groove.templateId = kGrooveDrumReference;
            track.groove.amount = 1.0f;
        }
    }
    applyConstraints(pattern, constraintSettingsFor(pattern, style));
}

void removeRhythmReference(Pattern& pattern, const StyleProfile& style) {
    pattern.rhythmRef.reset();
    pattern.kickGridId = style.kickDefault;
    for (Track& track : pattern.voices) {
        if (track.groove.templateId == kGrooveDrumReference) { // never a voice that plays as written: apply skips it
            track.groove.templateId = kGrooveStraight;
        }
    }
    applyConstraints(pattern, constraintSettingsFor(pattern, style));
}

} // namespace mm::core
