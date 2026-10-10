#include "core/Analysis.h"

#include "core/ChordSymbol.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace mm::core {

namespace {

constexpr uint32_t kBarTicks = 3840;
constexpr uint32_t kHalfBarTicks = kBarTicks / 2;
constexpr uint32_t kStepTicks = 240;
constexpr double kFirstBarBassFactor = 6.0;

using Profile = std::array<double, 12>;

double total(const Profile& profile) {
    double sum = 0;
    for (const double value : profile) {
        sum += value;
    }
    return sum;
}

/// Duration weights per pitch class. The lowest note(s) of each bar count twice (the bass names the harmony), the first
/// and last note of the clip 1.5 times (a loop starts and ends on its tonic).
Profile keyProfile(const std::vector<MidiClipNote>& notes) {
    Profile profile{};
    if (notes.empty()) {
        return profile;
    }
    std::map<uint32_t, uint8_t> lowestOfBar;
    for (const auto& note : notes) {
        const uint32_t bar = note.startTick / kBarTicks;
        const auto found = lowestOfBar.find(bar);
        if (found == lowestOfBar.end() || note.pitch < found->second) {
            lowestOfBar[bar] = note.pitch;
        }
    }
    size_t first = 0;
    size_t last = 0;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].startTick < notes[first].startTick) {
            first = i;
        }
        if (notes[i].startTick >= notes[last].startTick) {
            last = i;
        }
    }
    for (size_t i = 0; i < notes.size(); ++i) {
        const auto& note = notes[i];
        double weight = std::clamp<double>(note.lengthTicks, 120.0, 1920.0);
        if (note.pitch == lowestOfBar[note.startTick / kBarTicks]) {
            weight *= note.startTick < kBarTicks ? kFirstBarBassFactor : 2.0; // a loop starts on its tonic chord
        }
        if (i == first || i == last) {
            weight *= 1.5;
        }
        profile[note.pitch % 12] += weight;
    }
    return profile;
}

/// Log-likelihood of the profile under a model of (root, scale): the tones of the scale share almost all of the
/// probability (the root and the fifth get more), a tone outside costs. A scale with more tones spreads its share
/// thinner, so a melody of five tones fits the pentatonic scale better than the seven-tone one that holds it.
/// `evidence` is the number of notes the profile stands for (more notes, more weight against the prior).
double scoreKey(const Profile& profile, double sum, double evidence, PitchClass root, const Scale& scale,
                double prior) {
    constexpr double kOutside = 0.002;
    std::array<double, 12> weight{};
    double inside = 0;
    int outsideCount = 12;
    for (const uint8_t interval : scale.intervals) {
        const int pc = (root + interval) % 12;
        weight[pc] = interval == 0 ? 3.0 : (interval == 7 ? 1.5 : 1.0);
        inside += weight[pc];
        --outsideCount;
    }
    double score = std::log(prior);
    for (int pc = 0; pc < 12; ++pc) {
        const double q = weight[pc] > 0 ? (1.0 - kOutside) * weight[pc] / inside : kOutside / outsideCount;
        score += (profile[pc] / sum) * evidence * std::log(q);
    }
    return score;
}

struct Candidate {
    const Scale* scale;
    double prior; ///< 0 to 1, from the weights of the style
};

std::vector<Candidate> candidates(const AnalysisSettings& settings) {
    std::vector<Candidate> result;
    if (settings.candidateScales.empty()) {
        for (const auto& scale : allScales()) {
            result.push_back({&scale, 1.0});
        }
        return result;
    }
    double maxWeight = 1;
    for (size_t i = 0; i < settings.candidateScales.size(); ++i) {
        maxWeight = std::max<double>(maxWeight, i < settings.scaleWeights.size() ? settings.scaleWeights[i] : 1);
    }
    for (size_t i = 0; i < settings.candidateScales.size(); ++i) {
        if (const Scale* scale = findScale(settings.candidateScales[i])) {
            const double weight = i < settings.scaleWeights.size() ? settings.scaleWeights[i] : 1;
            result.push_back({scale, std::max(0.05, weight / maxWeight)});
        }
    }
    return result;
}

bool isDrumNote(uint8_t pitch) {
    return pitch >= 35 && pitch <= 59;
}

/// Groups the notes that start within a short time of each other (a played chord is never exactly together).
std::vector<std::vector<const MidiClipNote*>> onsetGroups(const std::vector<MidiClipNote>& notes) {
    std::vector<std::vector<const MidiClipNote*>> groups;
    for (const auto& note : notes) { // sorted by start
        if (!groups.empty() && note.startTick - groups.back().front()->startTick <= 30) {
            groups.back().push_back(&note);
        } else {
            groups.push_back({&note});
        }
    }
    return groups;
}

/// The triad quality the scale gives on a chord root. Never diminished: a diminished chord needs a tritone that sounds
/// (a scale only suggested by its prior must not turn every root chord of a bass line into a diminished one).
ChordQuality diatonicQuality(const Scale& scale, PitchClass keyRoot, int chordRootPc) {
    const bool minorThird = inScale(scale, keyRoot, chordRootPc + 3);
    const bool majorThird = inScale(scale, keyRoot, chordRootPc + 4);
    return majorThird && !minorThird ? ChordQuality::Major : ChordQuality::Minor;
}

struct WindowChord {
    Chord chord;
    double confidence = 0;
    double rootShare = 0; ///< how much of the bass weight of the window the root has (0 to 1)
    bool hasNotes = false;
};

/// The chord of the window [from, to): tones weighted by how long they sound inside it, the lowest ones twice.
WindowChord chordOfWindow(const std::vector<MidiClipNote>& notes, const std::vector<MidiClipNote>& bassNotes,
                          uint32_t from, uint32_t to, const KeyEstimate& key, const Scale& scale) {
    Profile profile{};
    int lowest = 128;
    for (const auto& note : notes) {
        const uint32_t end = note.startTick + note.lengthTicks;
        if (note.startTick < to && end > from) {
            lowest = std::min<int>(lowest, note.pitch);
        }
    }
    for (const auto& note : notes) {
        const uint32_t start = std::max(note.startTick, from);
        const uint32_t end = std::min(note.startTick + note.lengthTicks, to);
        if (note.startTick >= to || end <= from || end <= start) {
            continue;
        }
        double weight = static_cast<double>(end - start);
        if (note.pitch == lowest) {
            weight *= 2.0;
        }
        profile[note.pitch % 12] += weight;
    }
    WindowChord result;
    const double sum = total(profile);
    if (sum <= 0) {
        return result;
    }
    result.hasNotes = true;
    // The root comes from the bass: the tones that are the lowest sounding one when they start (root and fifth give the
    // root). The quality comes from all tones by template matching: the triad that holds most of the weight and leaves
    // least outside; with no third in the window the quality the scale gives is preferred (a bass line has no thirds).
    Profile bass{};
    for (const auto& note : bassNotes) {
        const uint32_t start = std::max(note.startTick, from);
        const uint32_t end = std::min(note.startTick + note.lengthTicks, to);
        if (note.startTick >= to || end <= from || end <= start) {
            continue;
        }
        bool lowestAtStart = true;
        for (const auto& other : bassNotes) {
            if (other.pitch < note.pitch && other.startTick <= note.startTick &&
                other.startTick + other.lengthTicks > note.startTick) {
                lowestAtStart = false;
                break;
            }
        }
        if (lowestAtStart) {
            bass[note.pitch % 12] += static_cast<double>(end - start);
        }
    }
    int bestRoot = 0;
    double bestRootScore = -1;
    for (int root = 0; root < 12; ++root) {
        const double rootScore = bass[root] + 0.7 * bass[(root + 7) % 12];
        if (rootScore > bestRootScore) {
            bestRootScore = rootScore;
            bestRoot = root;
        }
    }
    const double bassSum = total(bass);
    result.rootShare = bassSum > 0 ? bass[bestRoot] / bassSum : 0; // the root itself, not its fifth
    const bool thirdPresent = profile[(bestRoot + 3) % 12] + profile[(bestRoot + 4) % 12] >= 0.15 * profile[bestRoot];
    const ChordQuality diatonic = diatonicQuality(scale, key.root, bestRoot);
    ChordQuality quality = diatonic;
    double bestScore = -1e18;
    // Sus chords are not detected: a melody that holds the second or the fourth would turn every root chord into one.
    for (const ChordQuality candidate : {ChordQuality::Minor, ChordQuality::Major, ChordQuality::Diminished}) {
        if (candidate == ChordQuality::Diminished &&
            !(profile[(bestRoot + 6) % 12] > 2.0 * profile[(bestRoot + 7) % 12])) {
            continue; // a diminished chord needs a tritone that clearly beats the fifth
        }
        double inside = 0;
        Chord shape;
        shape.rootOffset = 0;
        shape.quality = candidate;
        for (const PitchClass pc : chordPitchClasses(static_cast<PitchClass>(bestRoot), shape)) {
            inside += profile[pc];
        }
        double score = inside - 0.6 * (sum - inside);
        if (candidate == diatonic) {
            score += (thirdPresent ? 0.01 : 0.05) * sum;
        }
        if (score > bestScore) {
            bestScore = score;
            quality = candidate;
        }
    }
    result.chord.rootOffset = static_cast<uint8_t>((bestRoot - key.root + 12) % 12);
    result.chord.quality = quality;
    double explained = 0;
    for (const PitchClass pc : chordPitchClasses(key.root, result.chord)) {
        explained += profile[pc];
    }
    result.confidence = std::clamp(explained / sum, 0.0, 1.0);
    return result;
}

} // namespace

KeyEstimate estimateKey(const std::vector<MidiClipNote>& notes, const AnalysisSettings& settings) {
    KeyEstimate estimate;
    const Profile profile = keyProfile(notes);
    const double sum = total(profile);
    const auto scales = candidates(settings);
    if (sum <= 0 || scales.empty()) {
        estimate.scaleId = scales.empty() ? "natural_minor" : std::string(scales.front().scale->id);
        return estimate;
    }
    const double evidence = std::min<double>(static_cast<double>(notes.size()), 200.0);
    double best = -1e18;
    double second = -1e18;
    for (int root = 0; root < 12; ++root) {
        for (const auto& candidate : scales) {
            const double score =
                scoreKey(profile, sum, evidence, static_cast<PitchClass>(root), *candidate.scale, candidate.prior);
            if (score > best) {
                second = best;
                best = score;
                estimate.root = static_cast<PitchClass>(root);
                estimate.scaleId = std::string(candidate.scale->id);
            } else if (score > second) {
                second = score;
            }
        }
    }
    estimate.confidence = 1.0 - std::exp(-(best - second) / 3.0); // 3 nats ahead: about 0.63
    return estimate;
}

KeyEstimate estimateKeyOf(const std::vector<std::vector<MidiClipNote>>& tracks, const AnalysisSettings& settings) {
    std::vector<MidiClipNote> all;
    for (const auto& track : tracks) {
        all.insert(all.end(), track.begin(), track.end());
    }
    std::stable_sort(all.begin(), all.end(), [](const MidiClipNote& a, const MidiClipNote& b) {
        return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
    });
    return estimateKey(all, settings);
}

TrackRole suggestRole(const std::vector<MidiClipNote>& notes) {
    if (notes.empty()) {
        return TrackRole::Melody;
    }
    size_t channelTen = 0;
    size_t drumPitches = 0;
    for (const auto& note : notes) {
        channelTen += note.channel == 10 ? 1 : 0;
        drumPitches += isDrumNote(note.pitch) ? 1 : 0;
    }
    if (channelTen * 2 > notes.size()) {
        return TrackRole::Drums;
    }
    const auto groups = onsetGroups(notes);
    size_t chordGroups = 0;
    for (const auto& group : groups) {
        std::set<uint8_t> pitches;
        for (const auto* note : group) {
            pitches.insert(note->pitch);
        }
        chordGroups += pitches.size() >= 3 ? 1 : 0;
    }
    if (chordGroups * 10 >= groups.size() * 6) { // regularly three or more tones at once
        return TrackRole::Chords;
    }
    std::set<uint8_t> distinct;
    for (const auto& note : notes) {
        distinct.insert(note.pitch);
    }
    if (drumPitches == notes.size() && notes.size() >= 8 && distinct.size() <= 8 && distinct.size() >= 2) {
        return TrackRole::Drums;
    }
    bool monophonic = true;
    for (size_t i = 0; i + 1 < notes.size() && monophonic; ++i) {
        monophonic = notes[i].startTick + notes[i].lengthTicks <= notes[i + 1].startTick + 30;
    }
    std::vector<uint8_t> pitches;
    for (const auto& note : notes) {
        pitches.push_back(note.pitch);
    }
    std::sort(pitches.begin(), pitches.end());
    const int median = pitches[pitches.size() / 2];
    return monophonic && median < 55 ? TrackRole::Bass : TrackRole::Melody;
}

namespace {

std::vector<ChordGuess> progressionOf(const std::vector<MidiClipNote>& notes,
                                      const std::vector<MidiClipNote>& bassNotes, uint32_t lengthBars,
                                      const KeyEstimate& key) {
    std::vector<ChordGuess> progression;
    const Scale* scale = findScale(key.scaleId);
    if (scale == nullptr || lengthBars == 0) {
        return progression;
    }
    Chord previous;
    previous.rootOffset = 0;
    previous.quality = diatonicQuality(*scale, key.root, key.root);
    for (uint32_t bar = 0; bar < lengthBars; ++bar) {
        const uint32_t from = bar * kBarTicks;
        const auto first = chordOfWindow(notes, bassNotes, from, from + kHalfBarTicks, key, *scale);
        const auto second = chordOfWindow(notes, bassNotes, from + kHalfBarTicks, from + kBarTicks, key, *scale);
        // Two chords in a bar only when both halves clearly have their own root and the pair explains the tones better
        // than one chord for the bar; otherwise the passing tones of one half would split every bar.
        const auto whole = chordOfWindow(notes, bassNotes, from, from + kBarTicks, key, *scale);
        const bool split = first.hasNotes && second.hasNotes && !(first.chord == second.chord) &&
                           first.rootShare >= 0.45 && second.rootShare >= 0.45 && first.confidence >= 0.6 &&
                           second.confidence >= 0.6 &&
                           (first.confidence + second.confidence) / 2 >= whole.confidence + 0.15;
        if (split) {
            progression.push_back({first.chord, bar * 2, 1, first.confidence});
            progression.push_back({second.chord, bar * 2 + 1, 1, second.confidence});
            previous = second.chord;
            continue;
        }
        if (whole.hasNotes) {
            progression.push_back({whole.chord, bar * 2, 2, whole.confidence});
            previous = whole.chord;
        } else {
            progression.push_back({previous, bar * 2, 2, 0.0}); // an empty bar goes on with the chord before it
        }
    }
    return progression;
}

} // namespace

std::vector<ChordGuess> estimateProgression(const std::vector<MidiClipNote>& notes, uint32_t lengthBars,
                                            const KeyEstimate& key) {
    return progressionOf(notes, notes, lengthBars, key);
}

std::vector<ChordGuess> estimateProgressionOf(const std::vector<std::vector<MidiClipNote>>& tracks, uint32_t lengthBars,
                                              const KeyEstimate& key) {
    std::vector<MidiClipNote> all;
    const std::vector<MidiClipNote>* bass = nullptr;
    int lowestMedian = 128;
    for (const auto& track : tracks) {
        all.insert(all.end(), track.begin(), track.end());
        if (track.empty()) {
            continue;
        }
        std::vector<uint8_t> pitches;
        for (const auto& note : track) {
            pitches.push_back(note.pitch);
        }
        std::sort(pitches.begin(), pitches.end());
        if (pitches[pitches.size() / 2] < lowestMedian) {
            lowestMedian = pitches[pitches.size() / 2];
            bass = &track; // the track that lies lowest names the harmony
        }
    }
    std::stable_sort(all.begin(), all.end(), [](const MidiClipNote& a, const MidiClipNote& b) {
        return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
    });
    return progressionOf(all, bass != nullptr ? *bass : all, lengthBars, key);
}

TrackAnalysis analyzeTrack(const std::vector<MidiClipNote>& notes, uint32_t lengthBars,
                           const AnalysisSettings& settings) {
    TrackAnalysis analysis;
    analysis.lengthBars = lengthBars;
    analysis.noteCount = notes.size();
    analysis.role = suggestRole(notes);
    analysis.key = estimateKey(notes, settings);
    analysis.progression = estimateProgression(notes, lengthBars, analysis.key);
    double weighted = 0;
    uint32_t halves = 0;
    for (const auto& guess : analysis.progression) {
        weighted += guess.confidence * guess.lengthHalfBars;
        halves += guess.lengthHalfBars;
    }
    analysis.progressionConfidence = halves > 0 ? weighted / halves : 0;

    RhythmStats& rhythm = analysis.rhythm;
    rhythm.bars.assign(lengthBars, {});
    std::array<long long, 16> offsetSum{};
    std::array<long long, 16> velocitySum{};
    std::array<int, 16> count{};
    for (const auto& note : notes) {
        const uint32_t step = (note.startTick + kStepTicks / 2) / kStepTicks;
        const uint32_t bar = step / 16;
        if (bar >= lengthBars) {
            continue;
        }
        const size_t inBar = step % 16;
        rhythm.bars[bar].set(inBar);
        offsetSum[inBar] += static_cast<long long>(note.startTick) - static_cast<long long>(step) * kStepTicks;
        velocitySum[inBar] += note.velocity;
        ++count[inBar];
    }
    for (size_t i = 0; i < 16; ++i) {
        if (count[i] > 0) {
            rhythm.meanOffsetTicks[i] = static_cast<int>(std::llround(static_cast<double>(offsetSum[i]) / count[i]));
            rhythm.meanVelocity[i] = static_cast<int>(std::llround(static_cast<double>(velocitySum[i]) / count[i]));
        }
    }
    rhythm.notesPerBar = lengthBars > 0 ? static_cast<double>(notes.size()) / lengthBars : 0;

    if (!notes.empty()) {
        std::vector<int> pitches;
        for (const auto& note : notes) {
            pitches.push_back(note.pitch);
        }
        std::sort(pitches.begin(), pitches.end());
        analysis.pitchRange.lowOffset = ((pitches.front() - analysis.key.root) % 12 + 12) % 12;
        analysis.pitchRange.span = pitches.back() - pitches.front();
        analysis.pitchRange.medianAbove = pitches[pitches.size() / 2] - pitches.front();
    }
    return analysis;
}

namespace {

const char* roleName(TrackRole role) {
    switch (role) {
    case TrackRole::Drums:
        return "drums";
    case TrackRole::Chords:
        return "chords";
    case TrackRole::Bass:
        return "bass";
    case TrackRole::Melody:
        return "melody";
    }
    return "melody";
}

std::optional<TrackRole> parseRole(const std::string& text) {
    for (const auto role : {TrackRole::Drums, TrackRole::Chords, TrackRole::Bass, TrackRole::Melody}) {
        if (text == roleName(role)) {
            return role;
        }
    }
    return std::nullopt;
}

} // namespace

std::string analysisToJson(const TrackAnalysis& analysis) {
    using nlohmann::json;
    json root;
    root["version"] = 1;
    root["role"] = roleName(analysis.role);
    root["lengthBars"] = analysis.lengthBars;
    root["noteCount"] = analysis.noteCount;
    root["key"] = {
        {"root", analysis.key.root}, {"scale", analysis.key.scaleId}, {"confidence", analysis.key.confidence}};
    json progression = json::array();
    for (const auto& guess : analysis.progression) {
        progression.push_back({{"chord", formatChordSymbol(guess.chord)},
                               {"start", guess.startHalfBar},
                               {"length", guess.lengthHalfBars},
                               {"confidence", guess.confidence}});
    }
    root["progression"] = progression;
    root["progressionConfidence"] = analysis.progressionConfidence;
    json bars = json::array();
    for (const auto& bar : analysis.rhythm.bars) {
        bars.push_back(bar.to_ulong());
    }
    root["rhythm"] = {{"bars", bars},
                      {"offsets", analysis.rhythm.meanOffsetTicks},
                      {"velocities", analysis.rhythm.meanVelocity},
                      {"notesPerBar", analysis.rhythm.notesPerBar}};
    root["register"] = {{"lowOffset", analysis.pitchRange.lowOffset},
                        {"span", analysis.pitchRange.span},
                        {"medianAbove", analysis.pitchRange.medianAbove}};
    return root.dump();
}

std::optional<TrackAnalysis> analysisFromJson(const std::string& text) {
    using nlohmann::json;
    const json root = json::parse(text, nullptr, false);
    if (!root.is_object() || root.value("version", 0) != 1) {
        return std::nullopt;
    }
    try {
        TrackAnalysis analysis;
        const auto role = parseRole(root.at("role").get<std::string>());
        if (!role) {
            return std::nullopt;
        }
        analysis.role = *role;
        analysis.lengthBars = root.at("lengthBars").get<uint32_t>();
        analysis.noteCount = root.at("noteCount").get<size_t>();
        const auto& key = root.at("key");
        const int keyRoot = key.at("root").get<int>();
        if (keyRoot < 0 || keyRoot > 11 || findScale(key.at("scale").get<std::string>()) == nullptr) {
            return std::nullopt;
        }
        analysis.key = {static_cast<PitchClass>(keyRoot), key.at("scale").get<std::string>(),
                        key.at("confidence").get<double>()};
        for (const auto& entry : root.at("progression")) {
            const auto chord = parseChordSymbol(entry.at("chord").get<std::string>());
            if (!chord) {
                return std::nullopt;
            }
            analysis.progression.push_back({*chord, entry.at("start").get<uint32_t>(),
                                            entry.at("length").get<uint32_t>(), entry.at("confidence").get<double>()});
        }
        analysis.progressionConfidence = root.at("progressionConfidence").get<double>();
        const auto& rhythm = root.at("rhythm");
        for (const auto& bar : rhythm.at("bars")) {
            analysis.rhythm.bars.emplace_back(bar.get<unsigned long>());
        }
        const auto offsets = rhythm.at("offsets").get<std::vector<int>>();
        const auto velocities = rhythm.at("velocities").get<std::vector<int>>();
        if (offsets.size() != 16 || velocities.size() != 16 || analysis.rhythm.bars.size() != analysis.lengthBars) {
            return std::nullopt;
        }
        std::copy(offsets.begin(), offsets.end(), analysis.rhythm.meanOffsetTicks.begin());
        std::copy(velocities.begin(), velocities.end(), analysis.rhythm.meanVelocity.begin());
        analysis.rhythm.notesPerBar = rhythm.at("notesPerBar").get<double>();
        const auto& range = root.at("register");
        analysis.pitchRange = {range.at("lowOffset").get<int>(), range.at("span").get<int>(),
                               range.at("medianAbove").get<int>()};
        if (analysis.pitchRange.lowOffset < 0 || analysis.pitchRange.lowOffset > 11 || analysis.pitchRange.span < 0 ||
            analysis.pitchRange.medianAbove < 0 || analysis.pitchRange.medianAbove > analysis.pitchRange.span) {
            return std::nullopt;
        }
        return analysis;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

} // namespace mm::core
