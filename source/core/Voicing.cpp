#include "core/Voicing.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace mm::core {

namespace {

PitchClass pcOf(PitchClass keyRoot, const Chord& chord, int interval) {
    return pitchClassOf(int(keyRoot) + int(chord.rootOffset) + interval);
}

bool contains(const std::vector<PitchClass>& list, PitchClass pc) {
    return std::find(list.begin(), list.end(), pc) != list.end();
}

void addUnique(std::vector<PitchClass>& list, PitchClass pc) {
    if (!contains(list, pc)) {
        list.push_back(pc);
    }
}

/// The first of the offered intervals whose pitch class is in the scale.
std::optional<int> pickInScale(const Scale& scale, PitchClass keyRoot, const Chord& chord,
                               std::initializer_list<int> intervals) {
    for (const int interval : intervals) {
        if (inScale(scale, keyRoot, pcOf(keyRoot, chord, interval))) {
            return interval;
        }
    }
    return std::nullopt;
}

int distanceToNearest(int pitch, const std::vector<int>& other) {
    int best = std::numeric_limits<int>::max();
    for (const int candidate : other) {
        best = std::min(best, std::abs(pitch - candidate));
    }
    return best;
}

int movementCost(const std::vector<int>& a, const std::vector<int>& b) {
    int cost = 0;
    for (const int pitch : a) {
        cost += distanceToNearest(pitch, b);
    }
    for (const int pitch : b) {
        cost += distanceToNearest(pitch, a);
    }
    return cost;
}

} // namespace

std::optional<ChordColor> parseChordColorId(std::string_view id) {
    if (id == "min") {
        return ChordColor::Min;
    }
    if (id == "fifth") {
        return ChordColor::Fifth;
    }
    if (id == "min7") {
        return ChordColor::Min7;
    }
    if (id == "min9") {
        return ChordColor::Min9;
    }
    if (id == "cluster") {
        return ChordColor::Cluster;
    }
    return std::nullopt;
}

std::optional<ChordColor> pickChordColor(std::span<const WeightedId> colors, Pcg32& rng) {
    std::vector<uint32_t> weights;
    weights.reserve(colors.size());
    for (const WeightedId& entry : colors) {
        const bool known = entry.id == "sus" || parseChordColorId(entry.id).has_value();
        weights.push_back(known && entry.weight > 0 ? static_cast<uint32_t>(entry.weight) : 0u);
    }
    const size_t index = rng.weightedIndex(weights);
    if (index >= colors.size()) {
        return std::nullopt;
    }
    if (colors[index].id == "sus") {
        return rng.chance(50) ? ChordColor::Sus2 : ChordColor::Sus4;
    }
    return parseChordColorId(colors[index].id);
}

std::vector<PitchClass> colorTones(const Scale& scale, PitchClass keyRoot, const Chord& chord, ChordColor color) {
    const auto triad = chordIntervals(chord.quality);
    const int third = triad[1];
    const int fifth = triad[2];
    std::vector<PitchClass> tones;
    auto add = [&](int interval) { addUnique(tones, pcOf(keyRoot, chord, interval)); };

    add(0);
    switch (color) {
    case ChordColor::Min:
        add(third);
        add(fifth);
        break;
    case ChordColor::Fifth:
        add(fifth);
        break;
    case ChordColor::Min7:
    case ChordColor::Min9: {
        add(third);
        const auto seventh = pickInScale(scale, keyRoot, chord, {10, 11});
        const auto ninth = color == ChordColor::Min9 ? pickInScale(scale, keyRoot, chord, {2, 1}) : std::nullopt;
        if (!ninth) {
            add(fifth);
        }
        if (seventh) {
            add(*seventh);
        }
        if (ninth) {
            add(*ninth);
        }
        break;
    }
    case ChordColor::Sus2:
        if (const auto second = pickInScale(scale, keyRoot, chord, {2})) {
            add(*second);
        }
        add(fifth);
        break;
    case ChordColor::Sus4:
        if (const auto fourth = pickInScale(scale, keyRoot, chord, {5})) {
            add(*fourth);
        }
        add(fifth);
        break;
    case ChordColor::Cluster:
        if (const auto flatSecond = pickInScale(scale, keyRoot, chord, {1})) {
            add(*flatSecond);
        }
        add(fifth);
        break;
    }
    // ascending order above the root; the root stays first
    std::sort(tones.begin() + 1, tones.end(), [&](PitchClass a, PitchClass b) {
        return pitchClassOf(int(a) - int(tones[0])) < pitchClassOf(int(b) - int(tones[0]));
    });
    return tones;
}

std::optional<std::vector<int>> voiceChord(std::span<const PitchClass> tones, Range range, bool doubleLowest,
                                           const std::vector<int>* previous) {
    std::vector<PitchClass> pcs;
    for (const PitchClass pc : tones) {
        addUnique(pcs, static_cast<PitchClass>(pc % 12));
    }
    if (pcs.empty() || pcs.size() > 4) {
        return std::nullopt;
    }
    const bool doubling = doubleLowest || pcs.size() < 3;
    if (pcs.size() + (doubling ? 1 : 0) < 3) {
        return std::nullopt;
    }

    std::optional<std::vector<int>> best;
    int bestCost = 0;
    auto better = [&](const std::vector<int>& candidate, int cost) {
        if (!best) {
            return true;
        }
        if (cost != bestCost) {
            return cost < bestCost;
        }
        const int span = candidate.back() - candidate.front();
        const int bestSpan = best->back() - best->front();
        if (span != bestSpan) {
            return span < bestSpan;
        }
        return candidate.front() < best->front();
    };

    const int rangeCentreTwice = range.low + range.high;
    for (size_t inversion = 0; inversion < pcs.size(); ++inversion) {
        const PitchClass lowest = pcs[inversion];
        // the other tones above the lowest one, nearest first: a close position
        std::vector<int> offsets;
        for (size_t i = 0; i < pcs.size(); ++i) {
            if (i != inversion) {
                offsets.push_back(pitchClassOf(int(pcs[i]) - int(lowest)));
            }
        }
        std::sort(offsets.begin(), offsets.end());
        for (int base = std::max(range.low, 0); base <= std::min(range.high, 127); ++base) {
            if (pitchClassOf(base) != lowest) {
                continue;
            }
            std::vector<int> candidate{base};
            for (const int offset : offsets) {
                candidate.push_back(base + offset);
            }
            if (doubling) {
                candidate.push_back(base + 12);
            }
            if (candidate.back() > range.high || candidate.back() > 127) {
                continue;
            }
            const int cost = previous != nullptr && !previous->empty()
                                 ? movementCost(candidate, *previous)
                                 : std::abs(candidate.front() + candidate.back() - rangeCentreTwice);
            if (better(candidate, cost)) {
                best = candidate;
                bestCost = cost;
            }
        }
    }
    return best;
}

std::optional<std::vector<std::vector<int>>> voiceProgression(const Scale& scale, PitchClass keyRoot,
                                                              std::span<const Chord> chords,
                                                              std::span<const ChordColor> colors,
                                                              std::span<const bool> doubleLowest, Range range) {
    if (chords.size() != colors.size() || chords.size() != doubleLowest.size()) {
        return std::nullopt;
    }
    std::vector<std::vector<int>> voicings;
    for (size_t i = 0; i < chords.size(); ++i) {
        const auto tones = colorTones(scale, keyRoot, chords[i], colors[i]);
        const std::vector<int>* previous = voicings.empty() ? nullptr : &voicings.back();
        auto voicing = voiceChord(tones, range, doubleLowest[i], previous);
        if (!voicing) {
            return std::nullopt;
        }
        voicings.push_back(std::move(*voicing));
    }
    return voicings;
}

int maxVoiceMovement(const std::vector<int>& from, const std::vector<int>& to) {
    if (from.empty() || to.empty()) {
        return 0;
    }
    int worst = 0;
    for (const int pitch : from) {
        worst = std::max(worst, distanceToNearest(pitch, to));
    }
    for (const int pitch : to) {
        worst = std::max(worst, distanceToNearest(pitch, from));
    }
    return worst;
}

} // namespace mm::core
