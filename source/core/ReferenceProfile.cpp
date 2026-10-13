#include "core/ReferenceProfile.h"

#include <algorithm>
#include <cmath>

namespace mm::core {

namespace {

constexpr uint32_t kStepTicks = kTicksPerBar / 16;

bool isLine(TrackRole role) {
    return role == TrackRole::Bass || role == TrackRole::Melody;
}

/// The chords of the track in order, equal neighbours merged, as the shortest cycle of 2 to 4 chords; empty if none.
std::vector<Chord> cycleOf(const std::vector<ChordGuess>& progression) {
    std::vector<Chord> sequence;
    for (const auto& guess : progression) {
        if (sequence.empty() || !(sequence.back() == guess.chord)) {
            sequence.push_back(guess.chord);
        }
    }
    const size_t longest = std::min<size_t>(4, sequence.size());
    for (size_t period = 2; period <= longest; ++period) {
        bool repeats = true;
        for (size_t i = period; i < sequence.size() && repeats; ++i) {
            repeats = sequence[i] == sequence[i % period];
        }
        if (repeats) {
            return {sequence.begin(), sequence.begin() + static_cast<std::ptrdiff_t>(period)};
        }
    }
    return {};
}

bool chordsLess(const std::vector<Chord>& a, const std::vector<Chord>& b) {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](const Chord& x, const Chord& y) {
        return x.rootOffset != y.rootOffset ? x.rootOffset < y.rootOffset : x.quality < y.quality;
    });
}

/// The share of 16th steps with a note start, in permille.
int densityPermille(const TrackAnalysis& analysis) {
    if (analysis.rhythm.bars.empty()) {
        return 0;
    }
    size_t steps = 0;
    for (const auto& bar : analysis.rhythm.bars) {
        steps += bar.count();
    }
    return static_cast<int>(steps * 1000 / (analysis.rhythm.bars.size() * 16));
}

PersonalExample exampleOf(const ReferenceItem& item) {
    PersonalExample example;
    example.role = item.role;
    example.root = item.analysis.key.root;
    example.scaleId = item.analysis.key.scaleId;
    example.bars = std::clamp<uint32_t>(item.lengthBars, 1, kMaxExampleBars);
    const uint32_t steps = example.bars * 16;
    int lowest = 127;
    for (const auto& note : item.notes) {
        if (note.startTick < example.bars * kTicksPerBar) {
            lowest = std::min<int>(lowest, note.pitch);
        }
    }
    const int below = ((lowest - example.root) % 12 + 12) % 12;
    const int base = lowest - below;
    for (const auto& note : item.notes) {
        if (note.startTick >= example.bars * kTicksPerBar || example.notes.size() >= kMaxExampleNotes) {
            continue;
        }
        ExampleNote out;
        out.step = std::min((note.startTick + kStepTicks / 2) / kStepTicks, steps - 1);
        out.semitones = note.pitch - base;
        out.lengthSteps = std::clamp<uint32_t>((note.lengthTicks + kStepTicks / 2) / kStepTicks, 1, steps - out.step);
        example.notes.push_back(out);
    }
    return example;
}

} // namespace

std::vector<ReferenceEntry> protectionEntries(const ReferenceSet& set) {
    std::vector<ReferenceEntry> entries;
    for (const auto& item : set.items) {
        if (!item.active || !isLine(item.role)) {
            continue;
        }
        ReferenceEntry entry;
        entry.role = item.role == TrackRole::Bass ? VoiceRole::Bass : VoiceRole::Melody;
        entry.bars = std::max<uint32_t>(item.lengthBars, 1);
        for (const auto& note : item.notes) {
            Note out;
            out.pitch = note.pitch;
            out.startTick = note.startTick;
            out.lengthTicks = note.lengthTicks;
            out.velocity = note.velocity;
            entry.notes.push_back(out);
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<WeightedProgression> personalProgressions(const ReferenceSet& set) {
    size_t active = 0;
    for (const auto& item : set.items) {
        active += item.active ? 1 : 0;
    }
    std::vector<WeightedProgression> result;
    if (active < kMinActiveForProgressions) {
        return result;
    }
    for (const auto& item : set.items) {
        if (!item.active || item.role == TrackRole::Drums ||
            item.analysis.progressionConfidence < kMinProgressionConfidence) {
            continue;
        }
        const auto chords = cycleOf(item.analysis.progression);
        if (chords.empty()) {
            continue;
        }
        const int percent = static_cast<int>(std::lround(item.analysis.progressionConfidence * 100.0));
        const int weight = std::max(1, item.weight * percent / 10);
        const auto found = std::find_if(result.begin(), result.end(),
                                        [&](const WeightedProgression& p) { return p.chords == chords; });
        if (found != result.end()) {
            found->weight += weight;
        } else {
            result.push_back({chords, weight});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const WeightedProgression& a, const WeightedProgression& b) { return chordsLess(a.chords, b.chords); });
    return result;
}

ReferenceEffect referenceEffectOf(const ReferenceSet& set) {
    ReferenceEffect effect;
    effect.entries = protectionEntries(set);
    effect.progressions = personalProgressions(set);
    const bool anyActive = std::any_of(set.items.begin(), set.items.end(), [](const ReferenceItem& i) { return i.active; });
    if (anyActive) {
        effect.setId = set.setId.empty() ? kReferenceSetId : set.setId;
    }
    return effect;
}

std::vector<PersonalExample> personalExamples(const ReferenceSet& set, int energyPct, size_t max) {
    const int target = 150 + std::clamp(energyPct, 0, 100) * 6;
    const auto better = [&](const ReferenceItem* a, const ReferenceItem* b) {
        if (a->weight != b->weight) {
            return a->weight > b->weight;
        }
        const int da = std::abs(densityPermille(a->analysis) - target);
        const int db = std::abs(densityPermille(b->analysis) - target);
        return da != db ? da < db : a->id < b->id;
    };
    std::vector<const ReferenceItem*> pool;
    for (const auto& item : set.items) {
        if (item.active && isLine(item.role) && !item.notes.empty()) {
            pool.push_back(&item);
        }
    }
    std::sort(pool.begin(), pool.end(), better);
    std::vector<const ReferenceItem*> chosen;
    for (const TrackRole role : {TrackRole::Bass, TrackRole::Melody}) {
        const auto first = std::find_if(pool.begin(), pool.end(), [&](const ReferenceItem* i) { return i->role == role; });
        if (first != pool.end() && chosen.size() < max) {
            chosen.push_back(*first);
        }
    }
    for (const ReferenceItem* item : pool) {
        if (chosen.size() < max && std::find(chosen.begin(), chosen.end(), item) == chosen.end()) {
            chosen.push_back(item);
        }
    }
    std::vector<PersonalExample> examples;
    for (const ReferenceItem* item : chosen) {
        examples.push_back(exampleOf(*item));
    }
    return examples;
}

} // namespace mm::core
