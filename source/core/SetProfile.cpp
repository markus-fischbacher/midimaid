#include "core/SetProfile.h"

#include "core/ChordSymbol.h"
#include "core/ReferenceProfile.h"

#include <algorithm>
#include <map>
#include <set>

namespace mm::core {

namespace {

constexpr uint32_t kStepTicks = kTicksPerBar / 16;

bool isLine(TrackRole role) {
    return role == TrackRole::Bass || role == TrackRole::Melody;
}

int stepOf(uint32_t tick) {
    return static_cast<int>((tick + kStepTicks / 2) / kStepTicks);
}

RoleStats& statsOf(SetProfile& profile, TrackRole role) {
    return role == TrackRole::Bass ? profile.bass : profile.melody;
}

int densityClass(TrackRole role, int notesPerBar100) {
    const int dense = (role == TrackRole::Bass ? kBassDenseNotesPerBar : kMelodyDenseNotesPerBar) * 100;
    const int calm = (role == TrackRole::Bass ? kBassCalmNotesPerBar : kMelodyCalmNotesPerBar) * 100;
    return notesPerBar100 >= dense ? 2 : (notesPerBar100 <= calm ? 0 : 1);
}

/// The cells of one item: every window of one bar and of two bars (not when its two bars are the same), once each.
std::set<std::tuple<uint8_t, std::vector<uint8_t>, std::vector<int8_t>>> cellsOfItem(const ReferenceItem& item) {
    std::set<std::tuple<uint8_t, std::vector<uint8_t>, std::vector<int8_t>>> cells;
    struct Quantised {
        int step;
        int pitch;
    };
    std::vector<Quantised> notes;
    for (const auto& note : item.notes) {
        notes.push_back({stepOf(note.startTick), note.pitch});
    }
    std::stable_sort(notes.begin(), notes.end(), [](const Quantised& a, const Quantised& b) {
        return a.step != b.step ? a.step < b.step : a.pitch < b.pitch;
    });
    const int bars = static_cast<int>(item.lengthBars);
    const auto window = [&](int firstBar, int windowBars) {
        std::vector<uint8_t> steps;
        std::vector<int8_t> intervals;
        int previous = -1;
        for (const auto& note : notes) {
            if (note.step < firstBar * 16 || note.step >= (firstBar + windowBars) * 16) {
                continue;
            }
            if (!steps.empty() && note.step - firstBar * 16 == steps.back()) {
                continue; // a chord: the first tone (the lowest) is the line
            }
            steps.push_back(static_cast<uint8_t>(note.step - firstBar * 16));
            intervals.push_back(static_cast<int8_t>(previous < 0 ? 0 : std::clamp(note.pitch - previous, -24, 24)));
            previous = note.pitch;
        }
        return std::make_pair(steps, intervals);
    };
    for (int bar = 0; bar < bars; ++bar) {
        auto [steps, intervals] = window(bar, 1);
        if (steps.size() >= kMinCellNotes && steps.size() <= kMaxCellNotes) {
            cells.insert({1, std::move(steps), std::move(intervals)});
        }
        if (bar + 1 < bars) {
            auto [two, twoIntervals] = window(bar, 2);
            // the same notes in both bars (rhythm and intervals): the cell of one bar says it already
            const auto [first, firstIntervals] = window(bar, 1);
            auto [second, secondIntervals] = window(bar + 1, 1);
            for (auto& step : second) {
                step = static_cast<uint8_t>(step + 16);
            }
            std::vector<uint8_t> both = first;
            both.insert(both.end(), second.begin(), second.end());
            const bool repeated = both == two && first.size() * 2 == two.size() && firstIntervals == secondIntervals &&
                                  std::equal(first.begin(), first.end(), second.begin(), [](uint8_t a, uint8_t b) { return a + 16 == b; });
            if (!repeated && two.size() >= kMinCellNotes && two.size() <= kMaxCellNotes) {
                cells.insert({2, std::move(two), std::move(twoIntervals)});
            }
        }
    }
    return cells;
}

} // namespace

std::vector<MotifCell> motifCellsOf(const ReferenceSet& set, TrackRole role) {
    std::map<std::tuple<uint8_t, std::vector<uint8_t>, std::vector<int8_t>>, int> counts;
    std::map<std::tuple<uint8_t, std::vector<uint8_t>, std::vector<int8_t>>, int> items;
    for (const auto& item : set.items) {
        if (!item.active || item.role != role || item.notes.empty()) {
            continue;
        }
        for (const auto& key : cellsOfItem(item)) {
            counts[key] += item.weight;
            items[key] += 1;
        }
    }
    std::vector<MotifCell> cells;
    for (const auto& [key, count] : counts) {
        if (items[key] < kMinCellItems) {
            continue;
        }
        MotifCell cell;
        cell.role = role;
        cell.bars = std::get<0>(key);
        cell.steps = std::get<1>(key);
        cell.intervals = std::get<2>(key);
        cell.count = count;
        cells.push_back(std::move(cell));
    }
    std::sort(cells.begin(), cells.end(), [](const MotifCell& a, const MotifCell& b) {
        if (a.count != b.count) {
            return a.count > b.count;
        }
        if (a.steps.size() != b.steps.size()) {
            return a.steps.size() > b.steps.size();
        }
        return std::tie(a.bars, a.steps, a.intervals) < std::tie(b.bars, b.steps, b.intervals);
    });
    if (cells.size() > kMaxMotifCells) {
        cells.resize(kMaxMotifCells);
    }
    return cells;
}

SetProfile profileOf(const ReferenceSet& set) {
    SetProfile profile;
    for (const auto& item : set.items) {
        profile.activeItems += item.active ? 1 : 0;
    }
    if (static_cast<size_t>(profile.activeItems) < kMinActiveForProgressions) {
        return profile;
    }
    profile.valid = true;
    std::map<std::string, int> scaleWeights;
    int64_t notesTotal = 0;
    int64_t notesOutside = 0;
    struct Sum {
        int64_t weight = 0;
        int64_t notesPerBar100 = 0;
        std::array<int64_t, 16> occupied{};
        int64_t bars = 0;
        int64_t span = 0;
        int64_t offsetSum = 0;
        int64_t offsetWeight = 0;
        int64_t velocity = 0;
        std::array<int64_t, 3> classes{};
    };
    Sum sums[2];
    for (const auto& item : set.items) {
        if (!item.active || item.role == TrackRole::Drums) {
            continue;
        }
        const int keyPercent = static_cast<int>(item.analysis.key.confidence * 100.0 + 0.5);
        scaleWeights[item.analysis.key.scaleId] += item.weight * std::max(1, keyPercent);
        if (!isLine(item.role)) {
            continue;
        }
        RoleStats& stats = statsOf(profile, item.role);
        ++stats.items;
        Sum& sum = sums[item.role == TrackRole::Bass ? 0 : 1];
        const int64_t weight = item.weight;
        sum.weight += weight;
        const int notesPerBar100 =
            item.lengthBars > 0 ? static_cast<int>(item.notes.size() * 100 / item.lengthBars) : 0;
        sum.notesPerBar100 += weight * notesPerBar100;
        sum.classes[static_cast<size_t>(densityClass(item.role, notesPerBar100))] += weight;
        for (const auto& bar : item.analysis.rhythm.bars) {
            ++sum.bars; // not weighted: the share of bars
            for (size_t step = 0; step < 16; ++step) {
                sum.occupied[step] += bar.test(step) ? 1 : 0;
            }
        }
        sum.span += weight * item.analysis.pitchRange.span;
        int velocitySum = 0;
        int velocityCount = 0;
        for (size_t step = 0; step < 16; ++step) {
            if (item.analysis.rhythm.meanVelocity[step] > 0) {
                velocitySum += item.analysis.rhythm.meanVelocity[step];
                ++velocityCount;
            }
            if (step % 4 == 2 && item.analysis.rhythm.meanVelocity[step] > 0) {
                sum.offsetSum += weight * item.analysis.rhythm.meanOffsetTicks[step];
                sum.offsetWeight += weight;
            }
        }
        sum.velocity += weight * (velocityCount > 0 ? velocitySum / velocityCount : 0);
        if (const Scale* scale = findScale(item.analysis.key.scaleId)) {
            for (const auto& note : item.notes) {
                ++notesTotal;
                notesOutside += inScale(*scale, item.analysis.key.root, note.pitch) ? 0 : 1;
            }
        }
    }
    for (int r = 0; r < 2; ++r) {
        RoleStats& stats = r == 0 ? profile.bass : profile.melody;
        const Sum& sum = sums[r];
        if (sum.weight == 0) {
            continue;
        }
        stats.notesPerBar100 = static_cast<int>(sum.notesPerBar100 / sum.weight);
        for (size_t step = 0; step < 16; ++step) {
            stats.stepPermille[step] = sum.bars > 0 ? static_cast<int>(sum.occupied[step] * 1000 / sum.bars) : 0;
        }
        stats.spanSemitones = static_cast<int>(sum.span / sum.weight);
        stats.offbeatOffsetTicks = sum.offsetWeight > 0 ? static_cast<int>(sum.offsetSum / sum.offsetWeight) : 0;
        stats.velocity = static_cast<int>(sum.velocity / sum.weight);
        for (size_t c = 0; c < 3; ++c) {
            stats.classPermille[c] = static_cast<int>(sum.classes[c] * 1000 / sum.weight);
        }
    }
    for (const auto& [id, weight] : scaleWeights) {
        profile.scales.push_back({id, weight});
    }
    profile.chromaticPermille = notesTotal > 0 ? static_cast<int>(notesOutside * 1000 / notesTotal) : 0;
    profile.progressions = personalProgressions(set);
    for (const TrackRole role : {TrackRole::Bass, TrackRole::Melody}) {
        const auto cells = motifCellsOf(set, role);
        profile.cells.insert(profile.cells.end(), cells.begin(), cells.end());
    }
    return profile;
}

ProfileFacts factsOf(const SetProfile& profile) {
    ProfileFacts facts;
    if (!profile.valid) {
        return facts;
    }
    if (!profile.progressions.empty()) {
        const auto best = std::max_element(profile.progressions.begin(), profile.progressions.end(),
                                           [](const auto& a, const auto& b) { return a.weight < b.weight; });
        for (const auto& chord : best->chords) {
            facts.progression += (facts.progression.empty() ? "" : "-") + formatChordSymbol(chord);
        }
    }
    facts.bassNotesPerBar100 = profile.bass.notesPerBar100;
    facts.melodyNotesPerBar100 = profile.melody.notesPerBar100;
    int total = 0;
    for (const auto& scale : profile.scales) {
        total += scale.weight;
    }
    const auto top = std::max_element(profile.scales.begin(), profile.scales.end(),
                                      [](const auto& a, const auto& b) { return a.weight < b.weight; });
    if (top != profile.scales.end() && total > 0) {
        facts.scaleId = top->id;
        facts.scalePercent = top->weight * 100 / total;
    }
    facts.cells = profile.cells.size();
    return facts;
}

} // namespace mm::core
