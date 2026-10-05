#include "core/Progression.h"

#include <algorithm>
#include <array>

namespace mm::core {

namespace {

constexpr std::array<uint32_t, 5> kStandardLengths{1, 2, 4, 8, 16};

const Chord kTonic{0, ChordQuality::Minor};

size_t chordCountOfMode(const std::string& mode) {
    if (mode == "two_chord") {
        return 2;
    }
    if (mode == "four_chord") {
        return 4;
    }
    return 0;
}

uint32_t clampedWeight(int weight) {
    return weight > 0 ? static_cast<uint32_t>(weight) : 0;
}

ChordEvent makeEvent(const Chord& chord, uint32_t startHalfBar, uint32_t lengthHalfBars) {
    ChordEvent event;
    event.chord = chord;
    event.startHalfBar = startHalfBar;
    event.lengthHalfBars = lengthHalfBars;
    return event;
}

} // namespace

std::vector<std::vector<Chord>> progressionsForMode(const HarmonyProfile& harmony, const std::string& mode) {
    if (mode == "static") {
        return {{kTonic}};
    }
    const size_t count = chordCountOfMode(mode);
    if (count == 0) {
        return {};
    }
    std::vector<std::vector<Chord>> result;
    for (const auto& progression : harmony.progressions) {
        if (progression.size() == count) {
            result.push_back(progression);
        }
    }
    if (!result.empty()) {
        return result;
    }
    for (const auto& progression : harmony.progressions) {
        if (count == 2 && progression.size() >= 2) {
            result.push_back({progression[0], progression[1]});
        } else if (count == 4 && progression.size() == 2) {
            result.push_back({progression[0], progression[1], progression[0], progression[1]});
        } else if (count == 4 && progression.size() == 3) {
            result.push_back({progression[0], progression[1], progression[2], progression[1]});
        }
    }
    return result;
}

std::vector<uint32_t> fittingChordLengths(const HarmonyProfile& harmony, size_t chordCount, uint32_t lengthBars) {
    std::vector<uint32_t> fitting;
    for (const uint32_t length : kStandardLengths) {
        const bool inRange = static_cast<int>(length) >= harmony.chordLengthBarsMin &&
                             static_cast<int>(length) <= harmony.chordLengthBarsMax;
        if (inRange && chordCount * length <= lengthBars) {
            fitting.push_back(length);
        }
    }
    if (!fitting.empty()) {
        return fitting;
    }
    uint32_t longest = 1;
    for (const uint32_t length : kStandardLengths) {
        if (chordCount * length <= lengthBars) {
            longest = length;
        }
    }
    return {longest};
}

std::vector<ChordEvent> generateProgression(const StyleProfile& style, uint32_t lengthBars, Pcg32& rng) {
    const uint32_t totalHalfBars = lengthBars * 2;
    const HarmonyProfile& harmony = style.harmony;

    std::vector<uint32_t> weights;
    weights.reserve(harmony.modes.size());
    for (const WeightedId& mode : harmony.modes) {
        const bool known = mode.id == "static" || chordCountOfMode(mode.id) != 0;
        weights.push_back(known ? clampedWeight(mode.weight) : 0);
    }
    const size_t modeIndex = rng.weightedIndex(weights);
    const std::string mode = modeIndex < harmony.modes.size() ? harmony.modes[modeIndex].id : "static";

    auto candidates = progressionsForMode(harmony, mode);
    if (candidates.empty()) {
        candidates = {{kTonic}};
    }
    std::vector<Chord> chords = candidates[rng.bounded(static_cast<uint32_t>(candidates.size()))];

    if (chords.size() == 1 || totalHalfBars == 0) {
        return {makeEvent(chords.front(), 0, std::max<uint32_t>(totalHalfBars, 1))};
    }
    uint32_t chordHalfBars = 1;
    if (lengthBars > 1) {
        const auto lengths = fittingChordLengths(harmony, chords.size(), lengthBars);
        chordHalfBars = lengths[rng.bounded(static_cast<uint32_t>(lengths.size()))] * 2;
    }

    std::vector<ChordEvent> events;
    uint32_t start = 0;
    for (size_t i = 0; start < totalHalfBars; ++i) {
        const uint32_t length = std::min(chordHalfBars, totalHalfBars - start);
        events.push_back(makeEvent(chords[i % chords.size()], start, length));
        start += length;
    }
    return events;
}

HarmonicContext makeHarmonicContext(const StyleProfile& style, uint32_t lengthBars, Pcg32& rng,
                                    std::optional<PitchClass> root, std::optional<std::string> scaleId) {
    HarmonicContext context;

    std::vector<uint32_t> scaleWeights;
    scaleWeights.reserve(style.scales.size());
    for (const WeightedId& scale : style.scales) {
        scaleWeights.push_back(clampedWeight(scale.weight));
    }
    const size_t scaleIndex = rng.weightedIndex(scaleWeights);
    if (scaleIndex < style.scales.size()) {
        context.scaleId = style.scales[scaleIndex].id;
    }
    const auto drawnRoot = static_cast<PitchClass>(rng.bounded(12));
    context.root = root.value_or(drawnRoot);
    context.progression = generateProgression(style, lengthBars, rng);
    if (scaleId) {
        context.scaleId = *scaleId;
    }
    return context;
}

void applyHarmony(Pattern& pattern, const StyleProfile& style, Pcg32& rng, std::optional<PitchClass> root,
                  std::optional<std::string> scaleId) {
    pattern.context = makeHarmonicContext(style, pattern.lengthBars, rng, root, std::move(scaleId));
}

} // namespace mm::core
