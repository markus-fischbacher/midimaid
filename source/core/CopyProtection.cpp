#include "core/CopyProtection.h"

#include "core/Motif.h"
#include "core/VelocityContour.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace mm::core {

namespace {

int stepsOf(const Material& material) {
    return static_cast<int>(material.bars) * kStepsPerBar;
}

} // namespace

Material materialOf(const Pattern& pattern, size_t voice) {
    const Track& track = pattern.voices[voice];
    return Material{track.role, pattern.lengthBars, track.notes};
}

Material materialOf(const ReferenceEntry& entry) {
    return Material{entry.role, entry.bars, entry.notes};
}

std::vector<LineOnset> lineOf(const Material& material) {
    std::vector<LineOnset> line;
    const int steps = stepsOf(material);
    if (steps <= 0) {
        return line;
    }
    const bool bass = material.role == VoiceRole::Bass;
    std::map<int, int> pitchAt; // step -> lowest (bass) or highest pitch
    for (const Note& note : material.notes) {
        const int step = static_cast<int>((note.startTick + kTicksPerStep / 2) / kTicksPerStep) % steps;
        const auto it = pitchAt.find(step);
        if (it == pitchAt.end()) {
            pitchAt[step] = note.pitch;
        } else {
            it->second = bass ? std::min<int>(it->second, note.pitch) : std::max<int>(it->second, note.pitch);
        }
    }
    for (const auto& [step, pitch] : pitchAt) {
        line.push_back(LineOnset{step, pitch, 0});
    }
    for (size_t i = 0; i < line.size(); ++i) {
        const size_t previous = (i + line.size() - 1) % line.size();
        line[i].interval = line.size() == 1 ? 0 : line[i].pitch - line[previous].pitch;
    }
    return line;
}

int similarityPermille(const Material& a, const Material& b) {
    const std::vector<LineOnset> lineA = lineOf(a);
    const std::vector<LineOnset> lineB = lineOf(b);
    if (lineA.empty() || lineB.empty()) {
        return 0;
    }
    const bool aShorter = a.bars <= b.bars;
    const Material& shortMaterial = aShorter ? a : b;
    const Material& longMaterial = aShorter ? b : a;
    const std::vector<LineOnset>& shortLine = aShorter ? lineA : lineB;
    const std::vector<LineOnset>& longLine = aShorter ? lineB : lineA;
    const int windowSteps = stepsOf(shortMaterial);
    const int longSteps = stepsOf(longMaterial);

    int best = 0;
    for (uint32_t bar = 0; bar < longMaterial.bars; ++bar) {
        const int windowStart = static_cast<int>(bar) * kStepsPerBar;
        std::map<int, int> intervalAt; // window step -> interval
        for (const LineOnset& onset : longLine) {
            const int relative = (onset.step - windowStart + longSteps) % longSteps;
            if (relative < windowSteps) {
                intervalAt[relative] = onset.interval;
            }
        }
        if (intervalAt.empty()) {
            continue;
        }
        int matches = 0;
        for (const LineOnset& onset : shortLine) {
            const auto it = intervalAt.find(onset.step);
            if (it != intervalAt.end() && it->second == onset.interval) {
                ++matches;
            }
        }
        const int denominator = static_cast<int>(std::max(shortLine.size(), intervalAt.size()));
        best = std::max(best, matches * 1000 / denominator);
    }
    return best;
}

bool isDistinctive(const ReferenceEntry& entry) {
    if (entry.bars == 0 || entry.notes.empty()) {
        return false;
    }
    std::set<int> pitchClasses;
    for (const Note& note : entry.notes) {
        pitchClasses.insert(note.pitch % 12);
    }
    if (static_cast<int>(pitchClasses.size()) < kDistinctiveMinPitchClasses) {
        return false;
    }
    const std::vector<LineOnset> line = lineOf(materialOf(entry));
    int changes = 0;
    for (size_t i = 1; i < line.size(); ++i) {
        changes += line[i].pitch != line[i - 1].pitch ? 1 : 0;
    }
    // changes per two bars: changes * 2 / bars >= 4
    return changes * 2 >= kDistinctiveMinChangesPerTwoBars * static_cast<int>(entry.bars);
}

CopyCheck checkCopyProtection(const Pattern& pattern, const std::vector<ReferenceEntry>& set) {
    CopyCheck result;
    bool any = false;
    for (size_t v = 0; v < pattern.voices.size(); ++v) {
        if (pattern.voices[v].notes.empty()) {
            continue;
        }
        const Material voice = materialOf(pattern, v);
        for (size_t e = 0; e < set.size(); ++e) {
            const ReferenceEntry& entry = set[e];
            if (!entry.active || entry.role != voice.role || !isDistinctive(entry)) {
                continue;
            }
            const int similarity = similarityPermille(voice, materialOf(entry));
            if (!any || similarity > result.permille) {
                any = true;
                result.voice = v;
                result.entry = e;
                result.permille = similarity;
            }
        }
    }
    result.violated = any && result.permille > kCopyThresholdPermille;
    return result;
}

HardCheck copyProtectionCheck(std::vector<ReferenceEntry> set) {
    auto shared = std::make_shared<const std::vector<ReferenceEntry>>(std::move(set));
    return [shared](const Pattern& pattern) { return !checkCopyProtection(pattern, *shared).violated; };
}

} // namespace mm::core
