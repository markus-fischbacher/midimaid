#include "core/Analysis.h"
#include "core/ChordSymbol.h"
#include "core/PatternGenerator.h"
#include "core/StyleProfile.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>

using namespace mm::core;

#ifndef MIDIMAID_RESOURCE_DIR
#error "MIDIMAID_RESOURCE_DIR must point to the resources folder"
#endif

namespace {

StyleProfile shipped(const std::string& name) {
    std::ifstream stream(std::string(MIDIMAID_RESOURCE_DIR) + "/styles/" + name + ".json", std::ios::binary);
    REQUIRE(stream.good());
    std::stringstream buffer;
    buffer << stream.rdbuf();
    const auto result = loadStyleProfile(buffer.str());
    REQUIRE(result.ok());
    return *result.profile;
}

std::vector<MidiClipNote> notesOf(const Track& track, uint8_t channel) {
    std::vector<MidiClipNote> notes;
    for (const auto& note : track.notes) {
        notes.push_back({channel, note.pitch, note.velocity, note.startTick, note.lengthTicks});
    }
    return notes;
}

void sortNotes(std::vector<MidiClipNote>& notes) {
    std::stable_sort(notes.begin(), notes.end(), [](const MidiClipNote& a, const MidiClipNote& b) {
        return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
    });
}

enum class Variant { Plain, Swing, Jitter };

/// Swing: every second 16th comes late (56 %). Jitter: every start moves by up to +-20 ticks, like a played clip.
void applyVariant(std::vector<MidiClipNote>& notes, Variant variant, uint32_t& state) {
    for (auto& note : notes) {
        if (variant == Variant::Swing && (note.startTick / 240) % 2 == 1) {
            note.startTick += 48;
        } else if (variant == Variant::Jitter) {
            state = state * 1664525u + 1013904223u;
            const int shift = static_cast<int>((state >> 16) % 41) - 20;
            note.startTick = static_cast<uint32_t>(std::max<int>(0, static_cast<int>(note.startTick) + shift));
        }
    }
    sortNotes(notes);
}

struct Case {
    std::string style;
    uint64_t seed;
    PitchClass root;
    Pattern pattern;
};

std::vector<Case> corpus(const std::string& styleName, int perKey) {
    const auto style = shipped(styleName);
    std::vector<Case> cases;
    for (int seed = 1; seed <= perKey; ++seed) {
        for (int root = 0; root < 12; ++root) {
            GenerationRequest request;
            request.lengthBars = 4;
            request.seed = static_cast<uint64_t>(seed) * 977 + static_cast<uint64_t>(root);
            request.root = static_cast<PitchClass>(root);
            const auto result = generatePattern(style, request);
            REQUIRE(result.success);
            cases.push_back({styleName, request.seed, static_cast<PitchClass>(root), result.pattern});
        }
    }
    return cases;
}

AnalysisSettings settingsFor(const StyleProfile& style) {
    AnalysisSettings settings;
    for (const auto& scale : style.scales) {
        settings.candidateScales.push_back(scale.id);
        settings.scaleWeights.push_back(scale.weight);
    }
    return settings;
}

/// True when the clip's tones decide the scale: the true scale is the only smallest candidate (at the true root) that
/// holds every pitch class of the clip. A clip of five tones that fit the pentatonic scale does not say "natural
/// minor", whatever the generator drew; such files count for the root only (SPEC 12: the key of a file is known when
/// its content shows it).
bool identifiable(const std::vector<MidiClipNote>& notes, const HarmonicContext& truth,
                  const AnalysisSettings& settings) {
    std::array<bool, 12> used{};
    for (const auto& note : notes) {
        used[note.pitch % 12] = true;
    }
    size_t holding = 0;
    size_t smallest = 99;
    bool trueHolds = false;
    size_t trueSize = 0;
    for (const auto& id : settings.candidateScales) {
        const Scale* scale = findScale(id);
        std::array<bool, 12> pcs{};
        for (const uint8_t interval : scale->intervals) {
            pcs[(truth.root + interval) % 12] = true;
        }
        bool holds = true;
        for (int pc = 0; pc < 12; ++pc) {
            holds = holds && (!used[pc] || pcs[pc]);
        }
        if (id == truth.scaleId) {
            trueHolds = holds;
            trueSize = scale->size();
        }
        if (holds) {
            ++holding;
            smallest = std::min(smallest, scale->size());
        }
    }
    (void)holding;
    if (!trueHolds || trueSize != smallest) {
        return false;
    }
    for (const auto& id : settings.candidateScales) { // another holding scale of the same size would tie
        const Scale* scale = findScale(id);
        if (id == truth.scaleId || scale->size() != smallest) {
            continue;
        }
        std::array<bool, 12> pcs{};
        for (const uint8_t interval : scale->intervals) {
            pcs[(truth.root + interval) % 12] = true;
        }
        bool holds = true;
        for (int pc = 0; pc < 12; ++pc) {
            holds = holds && (!used[pc] || pcs[pc]);
        }
        if (holds) {
            return false;
        }
    }
    return true;
}

struct Tally {
    int total = 0;
    int rootRight = 0;
    int identifiableCount = 0;
    int exact = 0;   ///< root and scale right, on the files whose content shows the scale
    int sameSet = 0; ///< wrong root, but the same pitch set (another mode of it): counted as wrong, shown apart
    void add(const KeyEstimate& got, const HarmonicContext& truth, bool shows) {
        ++total;
        rootRight += got.root == truth.root ? 1 : 0;
        if (shows) {
            ++identifiableCount;
            exact += got.root == truth.root && got.scaleId == truth.scaleId ? 1 : 0;
        }
        if (got.root != truth.root) {
            const Scale* a = findScale(got.scaleId);
            const Scale* b = findScale(truth.scaleId);
            std::array<bool, 12> setA{};
            std::array<bool, 12> setB{};
            for (const uint8_t i : a->intervals) {
                setA[(got.root + i) % 12] = true;
            }
            for (const uint8_t i : b->intervals) {
                setB[(truth.root + i) % 12] = true;
            }
            sameSet += setA == setB ? 1 : 0;
        }
    }
    double rootRate() const { return total > 0 ? static_cast<double>(rootRight) / total : 0; }
    double exactRate() const { return identifiableCount > 0 ? static_cast<double>(exact) / identifiableCount : 0; }
};

const Chord* truthChordAt(const HarmonicContext& context, uint32_t halfBar) {
    for (const auto& event : context.progression) {
        if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
            return &event.chord;
        }
    }
    return nullptr;
}

const Chord* guessedChordAt(const std::vector<ChordGuess>& progression, uint32_t halfBar) {
    for (const auto& guess : progression) {
        if (halfBar >= guess.startHalfBar && halfBar < guess.startHalfBar + guess.lengthHalfBars) {
            return &guess.chord;
        }
    }
    return nullptr;
}

struct ChordTally {
    int halves = 0;
    int rootRight = 0;
    int fullRight = 0;
    double rootRate() const { return halves > 0 ? double(rootRight) / halves : 0; }
    double fullRate() const { return halves > 0 ? double(fullRight) / halves : 0; }
};

/// Chord accuracy over the half bars of a corpus. A file whose key root is wrong counts all its half bars as wrong.
ChordTally chordTally(const std::string& styleName, bool twoTracks) {
    const auto style = shipped(styleName);
    const auto settings = settingsFor(style);
    ChordTally tally;
    for (const auto& c : corpus(styleName, 20)) {
        auto bass = notesOf(c.pattern.voices[0], 1);
        auto melody = notesOf(c.pattern.voices[1], 2);
        auto merged = bass;
        if (twoTracks) {
            merged.insert(merged.end(), melody.begin(), melody.end());
            sortNotes(merged);
        }
        const auto analysis = analyzeTrack(merged, 4, settings);
        const auto progression =
            twoTracks ? estimateProgressionOf({bass, melody}, 4, analysis.key) : analysis.progression;
        for (uint32_t h = 0; h < 8; ++h) {
            ++tally.halves;
            if (analysis.key.root != c.pattern.context.root) {
                continue;
            }
            const Chord* truth = truthChordAt(c.pattern.context, h);
            const Chord* guess = guessedChordAt(progression, h);
            if (truth != nullptr && guess != nullptr) {
                tally.rootRight += truth->rootOffset == guess->rootOffset ? 1 : 0;
                tally.fullRight += *truth == *guess ? 1 : 0;
            }
        }
    }
    return tally;
}

} // namespace

TEST_CASE("the key of the corpus files is found, in all 12 keys, plain, with swing and unquantised",
          "[analysis][corpus]") {
    int rootRight = 0;
    int rootTotal = 0;
    int exact = 0;
    int shown = 0;
    int sameSet = 0;
    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const auto style = shipped(name);
        const auto settings = settingsFor(style);
        std::map<std::string, Tally> tallies;
        uint32_t state = 99;
        for (const auto& c : corpus(name, 20)) {
            for (const auto variant : {Variant::Plain, Variant::Swing, Variant::Jitter}) {
                auto bass = notesOf(c.pattern.voices[0], 1);
                auto both = bass;
                const auto melody = notesOf(c.pattern.voices[1], 2);
                both.insert(both.end(), melody.begin(), melody.end());
                applyVariant(bass, variant, state);
                applyVariant(both, variant, state);
                tallies["bass"].add(estimateKey(bass, settings), c.pattern.context,
                                    identifiable(bass, c.pattern.context, settings));
                tallies["both"].add(estimateKey(both, settings), c.pattern.context,
                                    identifiable(both, c.pattern.context, settings));
            }
        }
        for (const auto& [label, t] : tallies) {
            INFO(name << " " << label << ": root " << t.rootRate() << ", exact " << t.exactRate() << " on "
                      << t.identifiableCount << " files, same pitch set " << t.sameSet);
            CHECK(t.rootRate() >= 0.95); // the root of a bass line or a mix
            if (t.identifiableCount >= 20) {
                CHECK(t.exactRate() >= 0.90); // root and scale, on files whose content shows the scale
            }
            CHECK(t.sameSet <= t.total / 25); // mix-ups with another mode of the same pitch set: apart
            rootRight += t.rootRight;
            rootTotal += t.total;
            exact += t.exact;
            sameSet += t.sameSet;
            ++shown;
        }
    }
    CHECK(shown == 6);
    CHECK(double(rootRight) / rootTotal >= 0.95);
    (void)exact;
    (void)sameSet;
}

TEST_CASE("the chords of the corpus files are found", "[analysis][corpus]") {
    struct Target {
        const char* style;
        bool twoTracks;
        double root;
        double full;
    };
    // Thresholds a little under what the corpus gives (regression guard). The melodic style is harder: its bass plays
    // the seventh and third of a chord in half bars without the root.
    for (const Target& target :
         {Target{"peak_time", false, 0.95, 0.90}, Target{"peak_time", true, 0.97, 0.93},
          Target{"melodic_techno", false, 0.80, 0.78}, Target{"melodic_techno", true, 0.80, 0.78},
          Target{"hard_industrial", false, 0.95, 0.95}, Target{"hard_industrial", true, 0.95, 0.80}}) {
        const auto tally = chordTally(target.style, target.twoTracks);
        INFO(target.style << (target.twoTracks ? " two tracks" : " bass") << ": root " << tally.rootRate()
                          << ", root and quality " << tally.fullRate());
        CHECK(tally.rootRate() >= target.root);
        CHECK(tally.fullRate() >= target.full);
    }
}

TEST_CASE("the corpus report prints the numbers behind the thresholds", "[.analysis-report]") {
    for (const char* name : {"peak_time", "melodic_techno", "hard_industrial"}) {
        const auto style = shipped(name);
        const auto settings = settingsFor(style);
        std::map<std::string, Tally> tallies;
        for (const auto& c : corpus(name, 20)) {
            const auto bass = notesOf(c.pattern.voices[0], 1);
            const auto melody = notesOf(c.pattern.voices[1], 2);
            auto both = bass;
            both.insert(both.end(), melody.begin(), melody.end());
            sortNotes(both);
            tallies["bass"].add(estimateKey(bass, settings), c.pattern.context,
                                identifiable(bass, c.pattern.context, settings));
            tallies["melody"].add(estimateKey(melody, settings), c.pattern.context,
                                  identifiable(melody, c.pattern.context, settings));
            tallies["both"].add(estimateKey(both, settings), c.pattern.context,
                                identifiable(both, c.pattern.context, settings));
        }
        for (const auto& [label, t] : tallies) {
            std::printf(
                "[analysis] %-15s %-7s root %.3f | scale shown in %3d/%3d files: exact %.3f | same pitch set %d\n",
                name, label.c_str(), t.rootRate(), t.identifiableCount, t.total, t.exactRate(), t.sameSet);
        }
        for (const bool two : {false, true}) {
            const auto tally = chordTally(name, two);
            std::printf("[analysis] %-15s chords %-10s root %.3f, root and quality %.3f\n", name,
                        two ? "two tracks" : "bass", tally.rootRate(), tally.fullRate());
        }
    }
}

namespace {

MidiClipNote note(uint8_t pitch, uint32_t start, uint32_t length = 240, uint8_t velocity = 100, uint8_t channel = 1) {
    return {channel, pitch, velocity, start, length};
}

/// A bass line over `bars` bars: the given roots (one per bar), each as eight 16ths of root, fifth and octave.
std::vector<MidiClipNote> bassLine(const std::vector<int>& roots) {
    std::vector<MidiClipNote> notes;
    for (size_t bar = 0; bar < roots.size(); ++bar) {
        for (uint32_t step = 0; step < 16; step += 2) {
            const int offset = step % 8 == 6 ? 7 : 0;
            notes.push_back(
                note(static_cast<uint8_t>(roots[bar] + offset), static_cast<uint32_t>(bar) * 3840 + step * 240, 180));
        }
    }
    return notes;
}

AnalysisSettings onlyScales(std::vector<std::string> ids) {
    AnalysisSettings settings;
    settings.candidateScales = std::move(ids);
    return settings;
}

} // namespace

TEST_CASE("an empty clip gives no confidence and no crash", "[analysis]") {
    const auto key = estimateKey({});
    CHECK(key.confidence == 0);
    CHECK(estimateProgression({}, 2, key).size() == 2); // the bars exist, the chord is the key's own, unsure
    CHECK(estimateProgression({}, 0, key).empty());
    const auto analysis = analyzeTrack({}, 2);
    CHECK(analysis.noteCount == 0);
    CHECK(analysis.rhythm.notesPerBar == 0);
    CHECK(suggestRole({}) == TrackRole::Melody);
}

TEST_CASE("the key is read from the tones, the bass and the loop start", "[analysis]") {
    // A natural minor: A B C D E F G, the line leans on A
    std::vector<MidiClipNote> line;
    uint32_t tick = 0;
    for (int round = 0; round < 4; ++round) {
        for (const int pitch : {45, 47, 48, 50, 52, 53, 55, 45, 45, 52, 45, 48}) {
            line.push_back(note(static_cast<uint8_t>(pitch), tick, pitch == 45 ? 480 : 240));
            tick += pitch == 45 ? 480 : 240;
        }
    }
    const auto key = estimateKey(line);
    CHECK(key.root == 9);
    CHECK(key.scaleId == "natural_minor");
    CHECK(key.confidence > 0.3);
}

TEST_CASE("the candidate scales limit the choice and their weights are the prior", "[analysis]") {
    const auto line = bassLine({45, 45}); // only A and E: no scale shows
    auto settings = onlyScales({"phrygian", "natural_minor"});
    settings.scaleWeights = {1, 5};
    CHECK(estimateKey(line, settings).scaleId == "natural_minor");
    settings.scaleWeights = {5, 1};
    CHECK(estimateKey(line, settings).scaleId == "phrygian");
    CHECK(estimateKey(line, onlyScales({"dorian"})).scaleId == "dorian");
    CHECK(estimateKey(line, onlyScales({"no_such_scale"})).confidence == 0); // nothing to choose from
    CHECK(estimateKey(line, onlyScales({"no_such_scale", "dorian"})).scaleId == "dorian");
}

TEST_CASE("a tone that only one scale has decides against the prior, five tones give the pentatonic scale",
          "[analysis]") {
    auto settings = onlyScales({"natural_minor", "phrygian", "minor_pentatonic"});
    settings.scaleWeights = {5, 1, 1};
    auto line = bassLine({45, 45});
    line.push_back(note(46, 3840 - 240, 480)); // the flat second of A: phrygian
    line.push_back(note(46, 3840 + 480, 480));
    CHECK(estimateKey(line, settings).scaleId == "phrygian");

    std::vector<MidiClipNote> pentatonic;
    uint32_t tick = 0;
    for (int round = 0; round < 6; ++round) {
        for (const int pitch : {45, 48, 50, 52, 55}) { // A C D E G
            pentatonic.push_back(note(static_cast<uint8_t>(pitch), tick));
            tick += 240;
        }
    }
    const auto key = estimateKey(pentatonic, settings);
    CHECK(key.root == 9);
    CHECK(key.scaleId == "minor_pentatonic");
}

TEST_CASE("several tracks give one key together", "[analysis]") {
    std::vector<MidiClipNote> bass = bassLine({40, 40, 40, 40}); // E and B
    std::vector<MidiClipNote> lead;
    uint32_t tick = 0;
    for (int round = 0; round < 4; ++round) {
        for (const int pitch : {64, 66, 67, 69, 71, 72, 74, 64}) { // E F# G A B C D: E natural minor
            lead.push_back(note(static_cast<uint8_t>(pitch), tick, 480, 100, 2));
            tick += 480;
        }
    }
    const auto key = estimateKeyOf({bass, lead});
    CHECK(key.root == 4);
    CHECK(key.scaleId == "natural_minor");
    CHECK(estimateKeyOf({}).confidence == 0);
}

TEST_CASE("the role is suggested from channel, notes, tones at once and register", "[analysis]") {
    std::vector<MidiClipNote> drums;
    for (uint32_t i = 0; i < 16; ++i) {
        drums.push_back(note(i % 2 == 0 ? 36 : 42, i * 240, 120, 100, 10));
    }
    CHECK(suggestRole(drums) == TrackRole::Drums);
    for (auto& n : drums) {
        n.channel = 1; // the same notes on another channel: still the drum notes of a kit
    }
    CHECK(suggestRole(drums) == TrackRole::Drums);

    std::vector<MidiClipNote> chords;
    for (uint32_t bar = 0; bar < 4; ++bar) {
        for (const int pitch : {57, 60, 64}) {
            chords.push_back(note(static_cast<uint8_t>(pitch), bar * 3840 + (pitch == 60 ? 12 : 0), 1800));
        }
    }
    CHECK(suggestRole(chords) == TrackRole::Chords); // a played chord is never exactly together

    CHECK(suggestRole(bassLine({33, 33})) == TrackRole::Bass);

    std::vector<MidiClipNote> lead;
    for (uint32_t i = 0; i < 8; ++i) {
        lead.push_back(note(static_cast<uint8_t>(72 + (i % 3)), i * 480, 240));
    }
    CHECK(suggestRole(lead) == TrackRole::Melody);

    std::vector<MidiClipNote> lowOverlapping; // low, but two tones at once: not a bass line
    lowOverlapping.push_back(note(40, 0, 1920));
    lowOverlapping.push_back(note(47, 480, 1920));
    CHECK(suggestRole(lowOverlapping) == TrackRole::Melody);
}

TEST_CASE("the progression has a chord per bar, two in a bar that clearly changes, and goes on through rests",
          "[analysis]") {
    KeyEstimate key;
    key.root = 9;
    key.scaleId = "natural_minor";
    // bar 0: A, bar 1: G (bVII), bar 2: empty, bar 3: first half F (bVI), second half G (bVII)
    auto notes = bassLine({45, 43});
    for (uint32_t step = 0; step < 8; ++step) {
        notes.push_back(note(41, 3 * 3840 + step * 240, 180));        // F
        notes.push_back(note(43, 3 * 3840 + 1920 + step * 240, 180)); // G
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const MidiClipNote& a, const MidiClipNote& b) { return a.startTick < b.startTick; });
    const auto progression = estimateProgression(notes, 4, key);
    REQUIRE(progression.size() == 5);
    CHECK(progression[0] == ChordGuess{Chord{0, ChordQuality::Minor}, 0, 2, progression[0].confidence});
    CHECK(progression[1].chord == Chord{10, ChordQuality::Major});
    CHECK(progression[1].startHalfBar == 2);
    CHECK(progression[2].chord == progression[1].chord); // the empty bar: the chord before it, unsure
    CHECK(progression[2].confidence == 0);
    CHECK(progression[2].startHalfBar == 4);
    CHECK(progression[3].chord == Chord{8, ChordQuality::Major});
    CHECK(progression[3].lengthHalfBars == 1);
    CHECK(progression[4].chord == Chord{10, ChordQuality::Major});
    CHECK(progression[4].startHalfBar == 7);
    uint32_t covered = 0;
    for (const auto& guess : progression) {
        CHECK(guess.startHalfBar == covered); // gapless
        covered += guess.lengthHalfBars;
        CHECK(guess.confidence >= 0);
        CHECK(guess.confidence <= 1);
    }
    CHECK(covered == 8);
    CHECK(estimateProgression(notes, 4, KeyEstimate{9, "no_such_scale", 0}).empty());
}

TEST_CASE("a bass line without thirds gets the quality of its scale, a stack gets the quality it holds", "[analysis]") {
    KeyEstimate key;
    key.root = 9;
    key.scaleId = "natural_minor";
    CHECK(estimateProgression(bassLine({45}), 1, key)[0].chord == Chord{0, ChordQuality::Minor});
    CHECK(estimateProgression(bassLine({41}), 1, key)[0].chord == Chord{8, ChordQuality::Major}); // F in A minor
    std::vector<MidiClipNote> major; // A major triad over an A minor key: the third that sounds wins
    for (const int pitch : {45, 49, 52}) {
        major.push_back(note(static_cast<uint8_t>(pitch), 0, 3840));
    }
    CHECK(estimateProgression(major, 1, key)[0].chord == Chord{0, ChordQuality::Major});
    std::vector<MidiClipNote> diminished; // B D F over A minor
    for (const int pitch : {47, 50, 53}) {
        diminished.push_back(note(static_cast<uint8_t>(pitch), 0, 3840));
    }
    CHECK(estimateProgression(diminished, 1, key)[0].chord == Chord{2, ChordQuality::Diminished});
}

TEST_CASE("two tracks: the lowest one names the roots", "[analysis]") {
    KeyEstimate key;
    key.root = 9;
    key.scaleId = "natural_minor";
    const auto bass = bassLine({45});
    std::vector<MidiClipNote> lead; // a lead that holds C E G (the third, fifth, seventh above A)
    for (const int pitch : {72, 76, 79}) {
        lead.push_back(note(static_cast<uint8_t>(pitch), 0, 3840, 100, 2));
    }
    const auto progression = estimateProgressionOf({lead, bass}, 1, key);
    REQUIRE(progression.size() == 1);
    CHECK(progression[0].chord.rootOffset == 0);
}

TEST_CASE("rhythm, groove, density and register are measured on the 16th grid", "[analysis]") {
    std::vector<MidiClipNote> notes = {note(45, 0, 200, 100), note(45, 245, 200, 80), note(57, 1925, 200, 60),
                                       note(48, 3840 + 480, 200, 90), note(52, 3840 + 1000, 200, 70)};
    sortNotes(notes);
    const auto analysis = analyzeTrack(notes, 2);
    REQUIRE(analysis.rhythm.bars.size() == 2);
    CHECK(analysis.rhythm.bars[0].test(0));
    CHECK(analysis.rhythm.bars[0].test(1)); // 245 ticks: the second 16th
    CHECK(analysis.rhythm.bars[0].test(8)); // 1925 ticks: the ninth 16th
    CHECK(analysis.rhythm.bars[0].count() == 3);
    CHECK(analysis.rhythm.bars[1].test(2));
    CHECK(analysis.rhythm.bars[1].test(4)); // 1000 ticks: nearest to step 4 (960)
    CHECK(analysis.rhythm.meanOffsetTicks[1] == 5);
    CHECK(analysis.rhythm.meanOffsetTicks[8] == 5);
    CHECK(analysis.rhythm.meanOffsetTicks[4] == 40);
    CHECK(analysis.rhythm.meanVelocity[0] == 100);
    CHECK(analysis.rhythm.meanVelocity[1] == 80);
    CHECK(analysis.rhythm.meanVelocity[3] == 0);
    CHECK(analysis.rhythm.notesPerBar == 2.5);
    CHECK(analysis.noteCount == 5);
    CHECK(analysis.pitchRange.lowOffset == ((45 - analysis.key.root) % 12 + 12) % 12);
    CHECK(analysis.pitchRange.span == 12);       // 45 to 57
    CHECK(analysis.pitchRange.medianAbove == 3); // the median is 48
}

TEST_CASE("a clip moved by k semitones gives the same analysis with the root moved by k", "[analysis]") {
    const auto style = shipped("peak_time");
    const auto settings = settingsFor(style);
    GenerationRequest request;
    request.lengthBars = 4;
    request.seed = 31;
    request.root = 9;
    const auto result = generatePattern(style, request);
    REQUIRE(result.success);
    auto both = notesOf(result.pattern.voices[0], 1);
    const auto lead = notesOf(result.pattern.voices[1], 2);
    both.insert(both.end(), lead.begin(), lead.end());
    sortNotes(both);
    const auto base = analyzeTrack(both, 4, settings);
    for (int shift = 1; shift < 12; ++shift) {
        auto moved = both;
        for (auto& n : moved) {
            n.pitch = static_cast<uint8_t>(n.pitch + shift);
        }
        const auto other = analyzeTrack(moved, 4, settings);
        INFO(shift);
        CHECK(other.key.root == (base.key.root + shift) % 12);
        CHECK(other.key.scaleId == base.key.scaleId);
        CHECK(other.key.confidence == base.key.confidence);
        CHECK(other.progression == base.progression); // chords are offsets from the root
        CHECK(other.rhythm == base.rhythm);
        CHECK(other.pitchRange == base.pitchRange);
        CHECK(other.role == base.role);
    }
}

TEST_CASE("the analysis is stored as JSON that does not depend on the key", "[analysis]") {
    const auto analysis = analyzeTrack(bassLine({45, 43, 45, 41}), 4, onlyScales({"natural_minor", "phrygian"}));
    const std::string text = analysisToJson(analysis);
    const auto back = analysisFromJson(text);
    REQUIRE(back.has_value());
    CHECK(back->role == analysis.role);
    CHECK(back->key.root == analysis.key.root);
    CHECK(back->key.scaleId == analysis.key.scaleId);
    CHECK(back->progression == analysis.progression);
    CHECK(back->rhythm == analysis.rhythm);
    CHECK(back->pitchRange == analysis.pitchRange);
    CHECK(back->lengthBars == 4);
    CHECK(back->noteCount == analysis.noteCount);
    CHECK(analysisToJson(*back) == text);

    // the same line a fifth higher: the document differs in the key root only
    auto moved = bassLine({45, 43, 45, 41});
    for (auto& n : moved) {
        n.pitch = static_cast<uint8_t>(n.pitch + 5);
    }
    auto original = nlohmann::json::parse(text);
    auto shifted =
        nlohmann::json::parse(analysisToJson(analyzeTrack(moved, 4, onlyScales({"natural_minor", "phrygian"}))));
    CHECK(shifted["key"]["root"].get<int>() == (analysis.key.root + 5) % 12);
    original["key"].erase("root");
    shifted["key"].erase("root");
    CHECK(original == shifted);
}

TEST_CASE("documents that are not an analysis are refused", "[analysis]") {
    const auto good = nlohmann::json::parse(analysisToJson(analyzeTrack(bassLine({45, 45}), 2)));
    CHECK_FALSE(analysisFromJson("").has_value());
    CHECK_FALSE(analysisFromJson("not json").has_value());
    CHECK_FALSE(analysisFromJson("[]").has_value());
    CHECK_FALSE(analysisFromJson("{}").has_value());
    const auto broken = [&](const std::function<void(nlohmann::json&)>& change) {
        auto copy = good;
        change(copy);
        return analysisFromJson(copy.dump()).has_value();
    };
    CHECK(broken([](nlohmann::json&) {}));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["version"] = 2; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["role"] = "tuba"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["key"]["root"] = 12; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["key"]["scale"] = "whole_tone"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["progression"][0]["chord"] = "zzz"; }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["rhythm"]["offsets"] = nlohmann::json::array({1, 2}); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["rhythm"]["bars"] = nlohmann::json::array({1}); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j.erase("register"); }));
    CHECK_FALSE(broken([](nlohmann::json& j) { j["lengthBars"] = "two"; }));
}

TEST_CASE("a weak passing third does not decide the quality of a chord", "[analysis]") {
    KeyEstimate key;
    key.root = 9;
    key.scaleId = "natural_minor";
    // C and G with a short E flat: in A natural minor the C chord is major, and the E flat is a passing tone
    const std::vector<MidiClipNote> notes = {note(48, 0, 1800), note(55, 0, 1800), note(63, 0, 120)};
    CHECK(estimateProgression(notes, 1, key)[0].chord == Chord{3, ChordQuality::Major});
    // with a third that sounds as long as the root, it is minor
    const std::vector<MidiClipNote> held = {note(48, 0, 1800), note(55, 0, 1800), note(63, 0, 1800)};
    CHECK(estimateProgression(held, 1, key)[0].chord == Chord{3, ChordQuality::Minor});
}

TEST_CASE("a tritone has to beat the fifth clearly to make a chord diminished", "[analysis]") {
    KeyEstimate key;
    key.root = 9;
    key.scaleId = "natural_minor";
    const std::vector<MidiClipNote> notes = {note(47, 0, 3000), note(50, 0, 3000), note(54, 0, 3000),
                                             note(53, 0, 1200)};
    CHECK(estimateProgression(notes, 1, key)[0].chord == Chord{2, ChordQuality::Minor}); // B D F# with a passing F
    // the F is louder than the F#, but not twice as loud: the template would call it diminished, the rule does not
    const std::vector<MidiClipNote> close = {note(47, 0, 3000), note(50, 0, 3000), note(53, 0, 2000),
                                             note(54, 0, 1500)};
    CHECK(estimateProgression(close, 1, key)[0].chord == Chord{2, ChordQuality::Minor});
    const std::vector<MidiClipNote> clear = {note(47, 0, 3000), note(50, 0, 3000), note(53, 0, 3200),
                                             note(54, 0, 1500)};
    CHECK(estimateProgression(clear, 1, key)[0].chord == Chord{2, ChordQuality::Diminished});
}

TEST_CASE("the confidence of a key grows with the evidence and stays between 0 and 1", "[analysis]") {
    const auto scaleLine = [](int rounds) {
        std::vector<MidiClipNote> line;
        uint32_t tick = 0;
        for (int round = 0; round < rounds; ++round) {
            for (const int pitch : {45, 47, 48, 50, 52, 53, 55, 45, 45, 52, 45, 48}) {
                line.push_back(note(static_cast<uint8_t>(pitch), tick, pitch == 45 ? 480 : 240));
                tick += pitch == 45 ? 480 : 240;
            }
        }
        return line;
    };
    const auto few = estimateKey(scaleLine(1));
    const auto many = estimateKey(scaleLine(6));
    CHECK(many.confidence > few.confidence);
    CHECK(many.confidence > 0.9);
    CHECK(many.confidence <= 1.0);
    CHECK(few.confidence >= 0.0);
}

TEST_CASE("channel 10 makes drums only when it holds most of the notes", "[analysis]") {
    std::vector<MidiClipNote> high; // channel 10 with pitches far above any drum kit
    for (uint32_t i = 0; i < 8; ++i) {
        high.push_back(note(static_cast<uint8_t>(70 + i), i * 240, 120, 100, 10));
    }
    CHECK(suggestRole(high) == TrackRole::Drums);
    std::vector<MidiClipNote> half; // exactly half on channel 10 is not "most of them"
    for (uint32_t i = 0; i < 8; ++i) {
        half.push_back(note(static_cast<uint8_t>(72 + i), i * 480, 240, 100, i % 2 == 0 ? 10 : 1));
    }
    CHECK(suggestRole(half) == TrackRole::Melody);
}

TEST_CASE("a few stacked tones do not make a chord track", "[analysis]") {
    std::vector<MidiClipNote> lead;
    for (uint32_t i = 0; i < 10; ++i) {
        lead.push_back(note(static_cast<uint8_t>(72 + (i % 4)), i * 480, 240));
        if (i < 3) { // three of ten onsets carry a triad
            lead.push_back(note(static_cast<uint8_t>(76 + (i % 4)), i * 480, 240));
            lead.push_back(note(static_cast<uint8_t>(79 + (i % 4)), i * 480, 240));
        }
    }
    sortNotes(lead);
    CHECK(suggestRole(lead) == TrackRole::Melody);
}

TEST_CASE("a half bar whose root is only a passing tone does not split the bar", "[analysis]") {
    KeyEstimate key;
    key.root = 9;
    key.scaleId = "natural_minor";
    // first half: A, second half: G C G E (the notes of C major, but the C is not the bass of the half bar)
    std::vector<MidiClipNote> notes;
    for (uint32_t i = 0; i < 4; ++i) {
        notes.push_back(note(45, i * 480, 400));
    }
    notes.push_back(note(43, 1920 + 480, 180));
    notes.push_back(note(48, 1920 + 960, 180));
    notes.push_back(note(43, 1920 + 1440, 120));
    notes.push_back(note(52, 1920 + 1680, 120));
    sortNotes(notes);
    const auto progression = estimateProgression(notes, 1, key);
    REQUIRE(progression.size() == 1);
    CHECK(progression[0].lengthHalfBars == 2);
}

TEST_CASE("a start close to the next 16th counts for that step", "[analysis]") {
    const auto analysis = analyzeTrack({note(45, 0), note(45, 230)}, 1);
    CHECK(analysis.rhythm.bars[0].test(0));
    CHECK(analysis.rhythm.bars[0].test(1)); // 230 ticks is nearer to step 1 (240) than to step 0
    CHECK(analysis.rhythm.meanOffsetTicks[1] == -10);
}
