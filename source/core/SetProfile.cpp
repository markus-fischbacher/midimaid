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

// ---------------------------------------------------------------------------------------------------------------
// The snapshot

#include "core/Archetype.h"
#include "core/ChordSymbol.h"

#include <nlohmann/json.hpp>

namespace mm::core {

namespace {

using nlohmann::json;

json roleToJson(const RoleStats& stats) {
    return {{"items", stats.items},   {"npb", stats.notesPerBar100}, {"steps", stats.stepPermille},
            {"span", stats.spanSemitones}, {"offset", stats.offbeatOffsetTicks}, {"vel", stats.velocity},
            {"classes", stats.classPermille}};
}

bool intIn(const json& value, int low, int high, int& out) {
    if (!value.is_number_integer()) {
        return false;
    }
    const auto number = value.get<int64_t>();
    if (number < low || number > high) {
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

template <size_t N> bool arrayIn(const json& value, int low, int high, std::array<int, N>& out) {
    if (!value.is_array() || value.size() != N) {
        return false;
    }
    for (size_t i = 0; i < N; ++i) {
        if (!intIn(value[i], low, high, out[i])) {
            return false;
        }
    }
    return true;
}

bool roleFromJson(const json& value, RoleStats& stats) {
    if (!value.is_object()) {
        return false;
    }
    return intIn(value.value("items", json()), 0, 500, stats.items) &&
           intIn(value.value("npb", json()), 0, 100000, stats.notesPerBar100) &&
           arrayIn(value.value("steps", json()), 0, 1000, stats.stepPermille) &&
           intIn(value.value("span", json()), 0, 127, stats.spanSemitones) &&
           intIn(value.value("offset", json()), -960, 960, stats.offbeatOffsetTicks) &&
           intIn(value.value("vel", json()), 0, 127, stats.velocity) &&
           arrayIn(value.value("classes", json()), 0, 1000, stats.classPermille);
}

} // namespace

std::string setProfileToJson(const SetProfile& profile) {
    if (!profile.valid) {
        return {};
    }
    json root;
    root["items"] = profile.activeItems;
    json scales = json::array();
    for (const auto& scale : profile.scales) {
        scales.push_back({scale.id, scale.weight});
    }
    root["scales"] = scales;
    root["chromatic"] = profile.chromaticPermille;
    root["bass"] = roleToJson(profile.bass);
    root["melody"] = roleToJson(profile.melody);
    json cells = json::array();
    for (const auto& cell : profile.cells) {
        cells.push_back({{"role", cell.role == TrackRole::Bass ? "bass" : "melody"},
                         {"bars", cell.bars},
                         {"steps", cell.steps},
                         {"intervals", cell.intervals},
                         {"count", cell.count}});
    }
    root["cells"] = cells;
    json progressions = json::array();
    for (const auto& progression : profile.progressions) {
        json chords = json::array();
        for (const auto& chord : progression.chords) {
            chords.push_back(formatChordSymbol(chord));
        }
        progressions.push_back({{"chords", chords}, {"weight", progression.weight}});
    }
    root["progressions"] = progressions;
    return root.dump();
}

std::optional<SetProfile> setProfileFromJson(std::string_view text) {
    const json root = json::parse(text.begin(), text.end(), nullptr, false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    try {
        SetProfile profile;
        profile.valid = true;
        if (!intIn(root.at("items"), static_cast<int>(kMinActiveForProgressions), 500, profile.activeItems) ||
            !intIn(root.at("chromatic"), 0, 1000, profile.chromaticPermille) ||
            !roleFromJson(root.at("bass"), profile.bass) || !roleFromJson(root.at("melody"), profile.melody)) {
            return std::nullopt;
        }
        std::set<std::string> seen;
        for (const auto& entry : root.at("scales")) {
            int weight = 0;
            if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !intIn(entry[1], 1, 1000000, weight) ||
                findScale(entry[0].get<std::string>()) == nullptr || !seen.insert(entry[0].get<std::string>()).second) {
                return std::nullopt;
            }
            profile.scales.push_back({entry[0].get<std::string>(), weight});
        }
        for (const auto& entry : root.at("cells")) {
            MotifCell cell;
            const auto role = entry.at("role").get<std::string>();
            if (role != "bass" && role != "melody") {
                return std::nullopt;
            }
            cell.role = role == "bass" ? TrackRole::Bass : TrackRole::Melody;
            int bars = 0;
            int count = 0;
            if (!intIn(entry.at("bars"), 1, 2, bars) || !intIn(entry.at("count"), 1, 1000000, count)) {
                return std::nullopt;
            }
            cell.bars = static_cast<uint8_t>(bars);
            cell.count = count;
            const auto& steps = entry.at("steps");
            const auto& intervals = entry.at("intervals");
            if (!steps.is_array() || !intervals.is_array() || steps.size() != intervals.size() ||
                steps.size() < kMinCellNotes || steps.size() > kMaxCellNotes) {
                return std::nullopt;
            }
            int previousStep = -1;
            for (size_t i = 0; i < steps.size(); ++i) {
                int step = 0;
                int interval = 0;
                if (!intIn(steps[i], 0, 16 * bars - 1, step) || step <= previousStep || !intIn(intervals[i], -24, 24, interval) ||
                    (i == 0 && interval != 0)) {
                    return std::nullopt;
                }
                previousStep = step;
                cell.steps.push_back(static_cast<uint8_t>(step));
                cell.intervals.push_back(static_cast<int8_t>(interval));
            }
            profile.cells.push_back(std::move(cell));
        }
        for (const auto& entry : root.at("progressions")) {
            WeightedProgression progression;
            if (!intIn(entry.at("weight"), 1, 1000000, progression.weight) || !entry.at("chords").is_array() ||
                entry.at("chords").size() < 2 || entry.at("chords").size() > 4) {
                return std::nullopt;
            }
            for (const auto& symbol : entry.at("chords")) {
                const auto chord = symbol.is_string() ? parseChordSymbol(symbol.get<std::string>()) : std::nullopt;
                if (!chord) {
                    return std::nullopt;
                }
                progression.chords.push_back(*chord);
            }
            profile.progressions.push_back(std::move(progression));
        }
        return profile;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

namespace {

/// The weight moved towards `ratioPermille` (the set's share over the style's), cut to the deviation limit, by
/// `personalPct` percent of the way. Never below 1. The result is in `kWeightResolution` times the unit of the style
/// (the weights of a style are 1 to 5, a change of 10 % would vanish in the rounding): all weights of one list get the
/// same factor, so only the ratios count in the weighted draws.
int movedWeight(int weight, int64_t ratioPermille, int personalPct) {
    ratioPermille = std::clamp<int64_t>(ratioPermille, 1000 - kMaxPersonalDeviationPermille,
                                        1000 + kMaxPersonalDeviationPermille);
    const int64_t factor = 1000 + (ratioPermille - 1000) * std::clamp(personalPct, 0, 100) / 100;
    return static_cast<int>(std::max<int64_t>(1, (static_cast<int64_t>(weight) * kWeightResolution * factor + 500) / 1000));
}

/// The weights of the archetypes of one role, moved by the density classes of the lines of the set.
void personalizeArchetypes(std::vector<WeightedId>& weights, const RoleStats& stats, int personalPct) {
    if (stats.items == 0) {
        return;
    }
    std::array<int64_t, 3> styleClass{};
    int64_t total = 0;
    const auto classOf = [](const Archetype* archetype) { return static_cast<size_t>(archetype->density); };
    for (const auto& entry : weights) {
        if (const Archetype* archetype = findArchetype(entry.id)) {
            styleClass[classOf(archetype)] += entry.weight;
            total += entry.weight;
        }
    }
    if (total == 0) {
        return;
    }
    for (auto& entry : weights) {
        const Archetype* archetype = findArchetype(entry.id);
        if (archetype == nullptr) {
            continue;
        }
        const size_t cls = classOf(archetype);
        if (styleClass[cls] == 0) {
            continue;
        }
        // the share of the set in this class over the share of the style (both in permille)
        const int64_t styleShare = styleClass[cls] * 1000 / total;
        const int64_t ratio = styleShare > 0 ? stats.classPermille[cls] * 1000 / styleShare : 1000;
        entry.weight = movedWeight(entry.weight, ratio, personalPct);
    }
}

} // namespace

StyleProfile personalizeStyle(const StyleProfile& style, const SetProfile& profile, int personalPct) {
    StyleProfile result = style;
    if (!profile.valid || personalPct <= 0) {
        return result;
    }
    // scales: only those the style has take part
    int64_t setTotal = 0;
    int64_t styleTotal = 0;
    for (const auto& scale : style.scales) {
        styleTotal += scale.weight;
        for (const auto& own : profile.scales) {
            if (own.id == scale.id) {
                setTotal += own.weight;
            }
        }
    }
    if (setTotal > 0 && styleTotal > 0) {
        for (auto& scale : result.scales) {
            int64_t own = 0;
            for (const auto& entry : profile.scales) {
                if (entry.id == scale.id) {
                    own = entry.weight;
                }
            }
            // (own / setTotal) / (weight / styleTotal)
            const int64_t ratio = scale.weight > 0 ? own * styleTotal * 1000 / (setTotal * scale.weight) : 1000;
            scale.weight = movedWeight(scale.weight, ratio, personalPct);
        }
    }
    personalizeArchetypes(result.bass.archetypes, profile.bass, personalPct);
    personalizeArchetypes(result.melody.archetypes, profile.melody, personalPct);
    // the chromatic share
    const int base = style.chromaticDefaultPercent;
    const int target = (profile.chromaticPermille + 5) / 10;
    const int bound = std::max(base / 2, kMinChromaticDeviationPercent);
    const int delta = std::clamp((target - base) * std::clamp(personalPct, 0, 100) / 100, -bound, bound);
    result.chromaticDefaultPercent = std::clamp(base + delta, 0, 100);
    return result;
}

std::vector<WeightedProgression> scaledProgressions(const std::vector<WeightedProgression>& progressions,
                                                    int personalPct) {
    std::vector<WeightedProgression> result;
    for (const auto& progression : progressions) {
        const int weight = static_cast<int>(static_cast<int64_t>(progression.weight) * std::clamp(personalPct, 0, 100) / 50);
        if (weight > 0) {
            result.push_back({progression.chords, weight});
        }
    }
    return result;
}

} // namespace mm::core
