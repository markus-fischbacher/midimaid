#include "core/ArchetypeMelody.h"

#include "core/Constraints.h"
#include "core/Motif.h"
#include "core/Quality.h"
#include "core/Voicing.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>

namespace mm::core {

namespace {

constexpr uint32_t kStepTicks = 240;
constexpr uint32_t kHalfBarTicks = kTicksPerBar / 2;
constexpr uint32_t kMinNoteTicks = 60;
constexpr uint32_t kBeatTicks = 960;
constexpr int kSteps = 16;

bool noteLess(const Note& a, const Note& b) {
    if (a.startTick != b.startTick) {
        return a.startTick < b.startTick;
    }
    return a.pitch < b.pitch;
}

bool isOffbeatStep(int step) {
    return step % 4 == 2;
}

class Melody {
public:
    Melody(const MelodyInput& input, Pcg32& rng)
        : in_(input), rng_(rng), endTick_(input.pattern.lengthBars * kTicksPerBar),
          energy_(std::clamp(input.energyPct, 0, 100)), creativity_(std::clamp(input.creativityPct, 0, 100)),
          chromatic_(std::max(input.style.chromaticDefaultPercent, 0)), tension_(chromatic_ >= 30) {
        for (const Track& track : input.pattern.voices) {
            if (track.role == VoiceRole::Bass) {
                bass_ = &track;
                break;
            }
        }
    }

    std::optional<std::vector<Note>> run() {
        const std::string_view id = in_.archetypeId;
        bool ok = true;
        if (id == "hypnotic_motif") {
            ok = buildHypnotic();
        } else if (id == "lead_phrase") {
            ok = buildLeadPhrase();
        } else if (id == "call_response") {
            ok = buildCallResponse();
        } else if (id == "pluck_seq") {
            buildPluck();
        } else if (id == "arp") {
            buildArp(false);
        } else if (id == "chord_arp") {
            buildArp(true);
        } else if (id == "stabs") {
            ok = buildStabs(false);
        } else if (id == "aggro_stabs") {
            ok = buildStabs(true);
        } else if (id == "acid_siren") {
            buildAcid();
        } else if (id == "atonal_motif") {
            buildAtonal();
        } else if (id == "sparse_hits") {
            buildSparse();
        } else {
            return std::nullopt;
        }
        if (!ok) {
            return std::nullopt;
        }
        finish();
        chromaticPass();
        if (id == "acid_siren") {
            addSlides();
        }
        std::sort(notes_.begin(), notes_.end(), noteLess);
        return std::move(notes_);
    }

private:
    // -- harmony helpers ---------------------------------------------------------------------------------------

    Chord chordAt(uint32_t tick) const {
        const uint32_t halfBar = tick / kHalfBarTicks;
        for (const ChordEvent& event : in_.pattern.context.progression) {
            if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
                return event.chord;
            }
        }
        return Chord{};
    }

    PitchClass keyRoot() const { return in_.pattern.context.root; }

    PitchClass rootPc(const Chord& chord) const { return static_cast<PitchClass>((keyRoot() + chord.rootOffset) % 12); }

    /// The pitch of a root a little above the bottom of the range, so that lines have room below and above.
    int baseFor(PitchClass pitchClass) const {
        int base = voiceBase(pitchClass, in_.range.low, in_.range.high).value_or(in_.range.low);
        if (base + 12 <= in_.range.high - 12) {
            base += 12;
        }
        return base;
    }

    /// Index of the scale step at or below `semitones` above the key root.
    int scaleIndex(int semitones) const {
        int index = 0;
        for (size_t i = 0; i < in_.scale.intervals.size(); ++i) {
            if (in_.scale.intervals[i] <= semitones % 12) {
                index = static_cast<int>(i);
            }
        }
        return index;
    }

    int chordRootStep(const Chord& chord) const { return scaleIndex(chord.rootOffset); }

    /// Scale steps by which a motif written for `reference` moves to follow `chord`.
    int chordShift(const Chord& reference, const Chord& chord) const {
        return chordRootStep(chord) - chordRootStep(reference);
    }

    /// A tension tone (`offset` semitones above the chord root) where the scale offers it, otherwise the nearest
    /// allowed pitch. The tone outside the scale is the wish of the chromatic pass (SPEC 3.8), see `addTension`.
    int tensionPitch(const Chord& chord, int offset) const {
        const int pitch = baseFor(rootPc(chord)) + offset;
        const auto here = std::optional<Chord>(chord);
        if (isAllowed(in_.scale, keyRoot(), here, pitch)) {
            return pitch;
        }
        return quantize(in_.scale, keyRoot(), here, pitch).value_or(pitch);
    }

    // -- density -----------------------------------------------------------------------------------------------

    /// Middle of the density band of the energy, in notes per bar.
    int midCount() const {
        const DensityBand band = densityBand(VoiceRole::Melody, energy_);
        return (band.low + band.high) / 200;
    }

    /// 40 % of the steps at energy 0 up to 100 % at energy 100, rounded up (arps, acid lines).
    size_t keptShare(size_t total) const {
        return std::min(total, (total * static_cast<size_t>(400 + 6 * energy_) + 999) / 1000);
    }

    bool variationBar(uint32_t relativeBar, uint32_t barCount) const { return barCount >= 4 && relativeBar % 4 == 3; }

    /// All positions 0..count-1 in random order; positions for which `first` holds come first.
    template <typename Pred> std::vector<int> shuffledOrder(int count, Pred first) {
        std::vector<int> a;
        std::vector<int> b;
        for (int i = 0; i < count; ++i) {
            (first(i) ? a : b).push_back(i);
        }
        rng_.shuffle(a.begin(), a.end());
        rng_.shuffle(b.begin(), b.end());
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    /// One note of the bar moves to another free position (without one it is dropped); the density stays.
    void moveOne(const std::vector<int>& domain, std::vector<int>& kept) {
        std::vector<int> absent;
        for (const int p : domain) {
            if (std::find(kept.begin(), kept.end(), p) == kept.end()) {
                absent.push_back(p);
            }
        }
        if (kept.empty()) {
            return;
        }
        kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(rng_.bounded(static_cast<uint32_t>(kept.size()))));
        if (!absent.empty()) {
            kept.push_back(absent[rng_.bounded(static_cast<uint32_t>(absent.size()))]);
        }
    }

    /// The kept positions of bar `relativeBar`: the first `count` of `order`, varied in the last bar of four.
    std::vector<int> keptFor(const std::vector<int>& order, size_t count, uint32_t relativeBar, uint32_t barCount) {
        std::vector<int> kept(order.begin(),
                              order.begin() + static_cast<std::ptrdiff_t>(std::min(count, order.size())));
        if (variationBar(relativeBar, barCount) && rng_.chance(creativity_)) {
            moveOne(order, kept);
        }
        std::sort(kept.begin(), kept.end());
        return kept;
    }

    void addNote(uint32_t start, uint32_t length, int pitch, bool accent) {
        if (start >= endTick_) {
            return;
        }
        Note note;
        note.startTick = start;
        note.lengthTicks = std::min(length, endTick_ - start);
        note.pitch = static_cast<uint8_t>(std::clamp(pitch, 0, 127));
        note.accent = accent;
        notes_.push_back(note);
    }

    /// A tension note: the allowed pitch nearest to the tension tone, remembering the tone itself for the chromatic
    /// pass (which plays it instead where the share of the style leaves room).
    void addTension(uint32_t start, uint32_t length, int offset) {
        const Chord chord = chordAt(start);
        const int pitch = tensionPitch(chord, offset);
        const int tone = baseFor(rootPc(chord)) + offset;
        if (tone != pitch) {
            wishes_[{start, pitch}] = tone;
        }
        addNote(start, length, pitch, false);
    }

    // -- phrases -----------------------------------------------------------------------------------------------

    struct Span {
        uint32_t startBar;
        uint32_t lengthBars;
    };

    std::vector<Span> phrases() const {
        std::vector<Span> spans;
        for (const Phrase& phrase : in_.pattern.phrases) {
            if (phrase.lengthBars >= 1 && phrase.startBar + phrase.lengthBars <= in_.pattern.lengthBars) {
                spans.push_back({phrase.startBar, phrase.lengthBars});
            }
        }
        if (spans.empty()) {
            spans.push_back({0, in_.pattern.lengthBars});
        }
        return spans;
    }

    // -- motif based archetypes --------------------------------------------------------------------------------

    MotifRules motifRules(const Chord& reference, int maxSpan, int topMargin) const {
        MotifRules rules;
        rules.scale = &in_.scale;
        rules.keyRoot = keyRoot();
        rules.referenceChord = reference;
        rules.basePitch = baseFor(keyRoot());
        rules.range = {in_.range.low, in_.range.high - topMargin};
        rules.maxSpanSemitones = maxSpan;
        return rules;
    }

    void realizeBlock(const Motif& motif, const MotifRules& rules, uint32_t startTick) {
        const int shift = chordShift(rules.referenceChord, chordAt(startTick));
        for (Note note : realizeMotif(motif, rules, shift, startTick)) {
            if (note.startTick < endTick_) {
                note.lengthTicks = std::min(note.lengthTicks, endTick_ - note.startTick);
                notes_.push_back(note);
            }
        }
    }

    bool buildHypnotic() {
        const int perBar = midCount();
        const uint32_t bars = in_.pattern.lengthBars >= 2 && perBar * 2 <= 6 && rng_.chance(50) ? 2u : 1u;
        const int count = std::clamp(perBar * static_cast<int>(bars), 3, 6);
        MotifParams params;
        params.bars = static_cast<uint8_t>(bars);
        params.minNotes = count;
        params.maxNotes = count;
        const MotifRules rules = motifRules(chordAt(0), 7, 0);
        const auto motif = generateMotif(params, rules, rng_);
        if (!motif.has_value()) {
            return false;
        }
        const auto sequence =
            buildMotifSequence(*motif, rules, in_.pattern.lengthBars, RepetitionStyle::Hypnotic, rng_);
        if (sequence.empty()) {
            return false;
        }
        for (size_t b = 0; b < sequence.size(); ++b) {
            realizeBlock(sequence[b], rules, static_cast<uint32_t>(b) * bars * kTicksPerBar);
        }
        return true;
    }

    bool buildLeadPhrase() {
        for (const Span& span : phrases()) {
            const uint32_t startTick = span.startBar * kTicksPerBar;
            const uint32_t bars = span.lengthBars >= 4 ? 2u : 1u;
            MotifParams params;
            params.bars = static_cast<uint8_t>(bars);
            params.minNotes = std::clamp(midCount() * static_cast<int>(bars), 3, 8);
            params.maxNotes = params.minNotes;
            params.pickupPercent = 40; // pickups are preferred in phrases (STYLES.md 1.11)
            // span up to an octave, five semitones of headroom for the climax
            const MotifRules rules = motifRules(chordAt(startTick), 12, 5);
            const auto motif = generateMotif(params, rules, rng_);
            if (!motif.has_value()) {
                return false;
            }
            const auto sequence = buildMotifSequence(*motif, rules, span.lengthBars, RepetitionStyle::Standard, rng_);
            if (sequence.empty()) {
                return false;
            }
            const size_t first = notes_.size();
            for (size_t b = 0; b < sequence.size(); ++b) {
                realizeBlock(sequence[b], rules, startTick + static_cast<uint32_t>(b) * bars * kTicksPerBar);
            }
            shapePhrase(first, startTick, span.lengthBars * kTicksPerBar);
        }
        return true;
    }

    /// Ends the phrase on a chord tone and puts its highest note into the second or third quarter.
    void shapePhrase(size_t first, uint32_t startTick, uint32_t lengthTicks) {
        if (first >= notes_.size()) {
            return;
        }
        const auto begin = notes_.begin() + static_cast<std::ptrdiff_t>(first);
        const auto last = std::max_element(begin, notes_.end(),
                                           [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        const Chord ending = chordAt(last->startTick);
        for (int distance = 0; distance <= 6; ++distance) {
            bool done = false;
            for (const int direction : {-1, 1}) {
                const int pitch = last->pitch + direction * distance;
                if (in_.range.contains(pitch) && isChordTone(keyRoot(), ending, pitch)) {
                    last->pitch = static_cast<uint8_t>(pitch);
                    done = true;
                    break;
                }
            }
            if (done) {
                break;
            }
        }
        // climax
        int top = 0;
        for (auto it = begin; it != notes_.end(); ++it) {
            top = std::max<int>(top, it->pitch);
        }
        const auto firstTop = std::find_if(begin, notes_.end(), [&](const Note& n) { return n.pitch == top; });
        const uint32_t regionStart = startTick + lengthTicks / 4;
        const uint32_t regionEnd = startTick + lengthTicks * 3 / 4;
        if (firstTop->startTick >= regionStart && firstTop->startTick < regionEnd) {
            return;
        }
        const uint32_t centre = startTick + lengthTicks / 2;
        Note* best = nullptr;
        for (auto it = begin; it != notes_.end(); ++it) {
            if (&*it == &*last || it->startTick < regionStart || it->startTick >= regionEnd) {
                continue;
            }
            const auto distance = [&](const Note& n) {
                return n.startTick > centre ? n.startTick - centre : centre - n.startTick;
            };
            if (best == nullptr || distance(*it) < distance(*best)) {
                best = &*it;
            }
        }
        if (best == nullptr) {
            return;
        }
        for (int pitch = top + 1; pitch <= top + 4; ++pitch) {
            if (in_.range.contains(pitch) && inScale(in_.scale, keyRoot(), pitch)) {
                best->pitch = static_cast<uint8_t>(pitch);
                return;
            }
        }
    }

    bool buildCallResponse() {
        static constexpr std::array<MotifVariation, 3> kAnswers{MotifVariation::Transpose, MotifVariation::Invert,
                                                                MotifVariation::NewEnding};
        static constexpr std::array<MotifVariation, 2> kFallbacks{MotifVariation::ShiftNote, MotifVariation::Micro};
        for (const Span& span : phrases()) {
            const uint32_t startTick = span.startBar * kTicksPerBar;
            const uint32_t callBars = span.lengthBars == 1   ? 1u
                                      : span.lengthBars == 2 ? 1u
                                      : span.lengthBars == 4 ? 2u
                                                             : 4u;
            const bool answers = span.lengthBars >= 2;
            const uint32_t unitBars = answers ? callBars * 2 : 1;
            const uint32_t motifBars = std::min(2u, callBars);
            MotifParams params;
            params.bars = static_cast<uint8_t>(motifBars);
            params.minNotes = std::clamp(midCount() * static_cast<int>(motifBars), 3, 8);
            params.maxNotes = params.minNotes;
            const MotifRules rules = motifRules(chordAt(startTick), 9, 0);
            const auto call = generateMotif(params, rules, rng_);
            if (!call.has_value()) {
                return false;
            }
            Motif response = *call;
            if (answers) {
                std::array<MotifVariation, 3> order = kAnswers;
                rng_.shuffle(order.begin(), order.end());
                bool found = false;
                for (const MotifVariation kind : order) {
                    if (const auto varied = varyMotif(*call, kind, rules, rng_)) {
                        response = *varied;
                        found = true;
                        break;
                    }
                }
                for (size_t i = 0; i < kFallbacks.size() && !found; ++i) {
                    if (const auto varied = varyMotif(*call, kFallbacks[i], rules, rng_)) {
                        response = *varied;
                        found = true;
                    }
                }
            }
            for (uint32_t unit = 0; unit + unitBars <= span.lengthBars; unit += unitBars) {
                const uint32_t unitStart = startTick + unit * kTicksPerBar;
                for (uint32_t block = 0; block * motifBars < callBars; ++block) {
                    realizeBlock(*call, rules, unitStart + block * motifBars * kTicksPerBar);
                    if (answers) {
                        realizeBlock(response, rules, unitStart + (callBars + block * motifBars) * kTicksPerBar);
                    }
                }
            }
        }
        return true;
    }

    // -- step based archetypes ---------------------------------------------------------------------------------

    void buildPluck() {
        const int gridStep = rng_.chance(30 + (4 * energy_) / 10) ? 1 : 2; // 16th or 8th grid
        const int positions = kSteps / gridStep;
        const std::vector<int> order = shuffledOrder(positions, [](int) { return false; });
        const size_t count = static_cast<size_t>(std::min(midCount(), positions));
        const size_t cycle = 3 + rng_.bounded(4);
        std::array<int, 6> degrees{};
        std::array<bool, 6> accents{};
        for (size_t i = 0; i < cycle; ++i) {
            degrees[i] = static_cast<int>(rng_.bounded(7)) - 2;
            accents[i] = rng_.chance(35);
        }
        const uint32_t length = gridStep == 1 ? 180 : 240;
        const int base = baseFor(keyRoot());
        for (uint32_t bar = 0; bar < in_.pattern.lengthBars; ++bar) {
            for (const int position : keptFor(order, count, bar, in_.pattern.lengthBars)) {
                const uint32_t start = bar * kTicksPerBar + static_cast<uint32_t>(position * gridStep) * kStepTicks;
                const size_t index = static_cast<size_t>(position) % cycle;
                const int step = chordRootStep(chordAt(start)) + degrees[index];
                addNote(start, length, scaleStepToMidi(in_.scale, base, step), accents[index]);
            }
        }
    }

    enum class Direction { Up, Down, UpDown, Random };

    /// The chord tones of `chord` (triad, or with the seventh) over `octaves` octaves, ascending.
    std::vector<int> arpTones(const Chord& chord, bool seventh, int octaves) const {
        const auto pcs = colorTones(in_.scale, keyRoot(), chord, seventh ? ChordColor::Min7 : ChordColor::Min);
        int from = in_.range.low + 5;
        std::vector<int> pitches;
        for (int p = from; p < from + 12 * octaves && p <= in_.range.high; ++p) {
            if (std::find(pcs.begin(), pcs.end(), pitchClassOf(p)) != pcs.end()) {
                pitches.push_back(p);
            }
        }
        return pitches;
    }

    void buildArp(bool perPhrase) {
        const bool seventh = perPhrase;
        // `arp` plays one pattern over the whole voice, `chord_arp` draws a new one per phrase
        const std::vector<Span> spans = perPhrase ? phrases() : std::vector<Span>{{0, in_.pattern.lengthBars}};
        for (const Span& span : spans) {
            const uint32_t firstBar = span.startBar;
            const uint32_t barCount = span.lengthBars;
            const int gridStep = perPhrase && rng_.chance(50) ? 2 : 1;
            const int positions = kSteps / gridStep;
            const Direction direction = static_cast<Direction>(rng_.bounded(4));
            const int octaves = 1 + static_cast<int>(rng_.bounded(2));
            const uint32_t length = (gridStep == 1 ? 180u : 360u) + (rng_.chance(50) ? 60u : 0u);
            std::array<uint8_t, 8> random{};
            for (uint8_t& value : random) {
                value = static_cast<uint8_t>(rng_.bounded(8));
            }
            const bool accentBeats = rng_.chance(50);
            const std::vector<int> order = shuffledOrder(positions, [](int) { return false; });
            const size_t count = keptShare(static_cast<size_t>(positions));
            for (uint32_t b = 0; b < barCount; ++b) {
                const uint32_t bar = firstBar + b;
                for (const int position : keptFor(order, count, b, barCount)) {
                    const uint32_t start = bar * kTicksPerBar + static_cast<uint32_t>(position * gridStep) * kStepTicks;
                    const auto tones = arpTones(chordAt(start), seventh, octaves);
                    if (tones.empty()) {
                        continue;
                    }
                    const size_t m = tones.size();
                    const size_t i =
                        static_cast<size_t>(b) * static_cast<size_t>(positions) + static_cast<size_t>(position);
                    size_t index = i % m;
                    if (direction == Direction::Down) {
                        index = m - 1 - i % m;
                    } else if (direction == Direction::UpDown && m > 1) {
                        const size_t j = i % (2 * m - 2);
                        index = j < m ? j : 2 * m - 2 - j;
                    } else if (direction == Direction::Random) {
                        index = random[i % random.size()] % m;
                    }
                    const bool accent = accentBeats && (position * gridStep) % 4 == 0;
                    addNote(start, length, tones[index], accent);
                }
            }
        }
    }

    bool buildStabs(bool aggressive) {
        const auto& events = in_.pattern.context.progression;
        std::vector<Chord> chords;
        std::vector<ChordColor> colors;
        for (const ChordEvent& event : events) {
            chords.push_back(event.chord);
            colors.push_back(pickChordColor(in_.style.melody.chordColors, rng_).value_or(ChordColor::Min));
        }
        std::array<bool, 64> fixed{}; // no octave doubling; an array gives the span contiguous storage
        if (chords.empty() || chords.size() > fixed.size()) {
            return false;
        }
        const std::span<const bool> noDoubling(fixed.data(), chords.size());
        auto voicings = voiceProgression(in_.scale, keyRoot(), chords, colors, noDoubling, in_.range);
        if (!voicings.has_value()) {
            // a narrow range cannot hold every colour (a fifth needs 13 pitches with its octave): triads fit
            std::fill(colors.begin(), colors.end(), ChordColor::Min);
            voicings = voiceProgression(in_.scale, keyRoot(), chords, colors, noDoubling, in_.range);
        }
        if (!voicings.has_value()) {
            return false;
        }
        const std::vector<int> order = shuffledOrder(kSteps, [](int step) { return isOffbeatStep(step); });
        const size_t count = static_cast<size_t>(std::min(midCount(), kSteps));
        const std::array<uint32_t, 2> lengths =
            aggressive ? std::array<uint32_t, 2>{120, 240} : std::array<uint32_t, 2>{240, 480};
        const int accentChance = aggressive ? 60 : 20 + (3 * energy_) / 10;
        std::array<bool, kSteps> longHit{};
        std::array<bool, kSteps> accent{};
        for (int s = 0; s < kSteps; ++s) {
            longHit[static_cast<size_t>(s)] = rng_.chance(50);
            accent[static_cast<size_t>(s)] = rng_.chance(accentChance);
        }
        for (uint32_t bar = 0; bar < in_.pattern.lengthBars; ++bar) {
            for (const int step : keptFor(order, count, bar, in_.pattern.lengthBars)) {
                const uint32_t start = bar * kTicksPerBar + static_cast<uint32_t>(step) * kStepTicks;
                const uint32_t halfBar = start / kHalfBarTicks;
                for (size_t e = 0; e < events.size(); ++e) {
                    if (halfBar >= events[e].startHalfBar &&
                        halfBar < events[e].startHalfBar + events[e].lengthHalfBars) {
                        for (const int pitch : (*voicings)[e]) {
                            addNote(start, lengths[longHit[static_cast<size_t>(step)] ? 1 : 0], pitch,
                                    accent[static_cast<size_t>(step)]);
                        }
                        break;
                    }
                }
            }
        }
        return true;
    }

    void buildAcid() {
        const int reach = in_.scale.size() >= 7 ? 3 : 2; // up to an octave in either scale size
        std::array<int, kSteps> degree{};
        std::array<bool, kSteps> shortNote{};
        std::array<bool, kSteps> accent{};
        int current = 0;
        for (size_t s = 0; s < kSteps; ++s) {
            current = std::clamp(current + static_cast<int>(rng_.bounded(5)) - 2, -reach, reach);
            degree[s] = current;
            shortNote[s] = rng_.chance(30);
            accent[s] = rng_.chance(30 + (4 * energy_) / 10);
        }
        const std::vector<int> order = shuffledOrder(kSteps, [](int) { return false; });
        const size_t count = keptShare(kSteps);
        const int base = baseFor(keyRoot());
        for (uint32_t bar = 0; bar < in_.pattern.lengthBars; ++bar) {
            for (const int step : keptFor(order, count, bar, in_.pattern.lengthBars)) {
                const uint32_t start = bar * kTicksPerBar + static_cast<uint32_t>(step) * kStepTicks;
                const auto s = static_cast<size_t>(step);
                addNote(start, shortNote[s] ? 120 : 240, scaleStepToMidi(in_.scale, base, degree[s]), accent[s]);
            }
        }
    }

    /// Slide flags once the pitches are final: never on the last note and never onto the same pitch (a tie).
    void addSlides() {
        std::sort(notes_.begin(), notes_.end(), noteLess);
        const int chance = 25 + (2 * energy_) / 10;
        for (size_t i = 0; i + 1 < notes_.size(); ++i) {
            if (notes_[i].pitch != notes_[i + 1].pitch &&
                notes_[i].startTick + notes_[i].lengthTicks <= notes_[i + 1].startTick) {
                notes_[i].slide = rng_.chance(chance);
            }
        }
    }

    void buildAtonal() {
        const size_t count = static_cast<size_t>(2 + (2 * energy_) / 100);
        const std::vector<int> order = shuffledOrder(kSteps, [](int) { return false; });
        std::vector<int> steps(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count));
        std::sort(steps.begin(), steps.end());
        std::array<int, 4> offsets{};
        std::array<uint32_t, 4> lengths{};
        constexpr std::array<int, 3> kTension{0, 1, 6}; // root, flat second, tritone
        int previous = -1;
        for (size_t i = 0; i < count; ++i) {
            int offset = kTension[rng_.bounded(3)];
            while (offset == previous) {
                offset = kTension[rng_.bounded(3)];
            }
            offsets[i] = previous = offset;
            lengths[i] = rng_.chance(50) ? 240 : 480;
        }
        for (uint32_t bar = 0; bar < in_.pattern.lengthBars; ++bar) {
            for (size_t i = 0; i < count; ++i) {
                const uint32_t start = bar * kTicksPerBar + static_cast<uint32_t>(steps[i]) * kStepTicks;
                addTension(start, lengths[i], offsets[i]);
            }
        }
    }

    void buildSparse() {
        const uint32_t cellBars = std::min(2u, in_.pattern.lengthBars);
        const int positions = static_cast<int>(cellBars) * kSteps;
        const size_t count = static_cast<size_t>(1 + (3 * energy_) / 100);
        const std::vector<int> order = shuffledOrder(positions, [](int) { return false; });
        std::vector<int> steps(order.begin(),
                               order.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(count, order.size())));
        std::sort(steps.begin(), steps.end());
        constexpr std::array<int, 3> kTension{0, 1, 6};
        constexpr std::array<uint32_t, 3> kWeights{50, 30, 20};
        std::vector<int> offsets;
        std::vector<uint32_t> lengths;
        for (size_t i = 0; i < steps.size(); ++i) {
            offsets.push_back(kTension[rng_.weightedIndex(kWeights)]);
            lengths.push_back(rng_.chance(50) ? 240 : 480);
        }
        for (uint32_t cell = 0; cell * cellBars < in_.pattern.lengthBars; ++cell) {
            for (size_t i = 0; i < steps.size(); ++i) {
                const uint32_t start = cell * cellBars * kTicksPerBar + static_cast<uint32_t>(steps[i]) * kStepTicks;
                addTension(start, lengths[i], offsets[i]);
            }
        }
    }

    // -- the rules of the constraint layer, applied in advance --------------------------------------------------

    /// Folds a pitch into the range by octaves (as the constraint layer does); -1 if the range has no such pitch.
    int fold(int pitch) const {
        if (in_.range.contains(pitch)) {
            return pitch;
        }
        const int pc = pitchClassOf(pitch);
        if (pitch < in_.range.low) {
            const int candidate = in_.range.low + ((pc - in_.range.low) % 12 + 12) % 12;
            return candidate <= in_.range.high ? candidate : -1;
        }
        const int candidate = in_.range.high - ((in_.range.high - pc) % 12 + 12) % 12;
        return candidate >= in_.range.low ? candidate : -1;
    }

    /// Notes keep to the range: a pitch outside folds by octaves, a note without a place goes. Every generator builds
    /// pitches of scale and chord; the chromatic pass leaves the scale afterwards, within the share of the style.
    bool pitchPass() {
        bool changed = false;
        std::vector<Note> kept;
        for (Note note : notes_) {
            const int folded = fold(note.pitch);
            if (folded < 0) {
                changed = true;
                continue;
            }
            if (folded != note.pitch) {
                changed = true;
                note.pitch = static_cast<uint8_t>(folded);
            }
            kept.push_back(note);
        }
        notes_ = std::move(kept);
        return changed;
    }

    /// The register and interval rules against the bass: a violating note moves to the nearest allowed pitch (down
    /// before up) or goes.
    bool bassPass() {
        if (bass_ == nullptr || bass_->notes.empty()) {
            return false;
        }
        bool changed = false;
        std::vector<Note> kept;
        for (Note note : notes_) {
            if (!violatesBassRules(*bass_, note.startTick, note.lengthTicks, note.pitch, tension_)) {
                kept.push_back(note);
                continue;
            }
            changed = true;
            const auto chord = std::optional<Chord>(chordAt(note.startTick));
            bool fixed = false;
            for (int distance = 1; distance <= 24 && !fixed; ++distance) {
                for (const int direction : {-1, 1}) {
                    const int candidate = note.pitch + direction * distance;
                    if (!in_.range.contains(candidate) || !isAllowed(in_.scale, keyRoot(), chord, candidate) ||
                        violatesBassRules(*bass_, note.startTick, note.lengthTicks, candidate, tension_)) {
                        continue;
                    }
                    note.pitch = static_cast<uint8_t>(candidate);
                    fixed = true;
                    break;
                }
            }
            if (fixed) {
                kept.push_back(note);
            }
        }
        notes_ = std::move(kept);
        return changed;
    }

    /// Equal notes collapse, equal pitches never overlap, too short notes go (as in the constraint layer).
    bool tidyPass() {
        bool changed = false;
        std::sort(notes_.begin(), notes_.end(), noteLess);
        std::vector<Note> unique;
        for (const Note& note : notes_) {
            if (!unique.empty() && unique.back().startTick == note.startTick && unique.back().pitch == note.pitch) {
                unique.back().lengthTicks = std::max(unique.back().lengthTicks, note.lengthTicks);
                changed = true;
                continue;
            }
            unique.push_back(note);
        }
        for (size_t i = 0; i < unique.size(); ++i) {
            uint32_t limit = unique[i].startTick + unique[i].lengthTicks;
            for (size_t j = i + 1; j < unique.size() && unique[j].startTick < limit; ++j) {
                if (unique[j].pitch == unique[i].pitch) {
                    limit = unique[j].startTick;
                    break;
                }
            }
            if (limit < unique[i].startTick + unique[i].lengthTicks) {
                unique[i].lengthTicks = limit - unique[i].startTick;
                changed = true;
            }
        }
        std::vector<Note> kept;
        for (const Note& note : unique) {
            if (note.lengthTicks >= kMinNoteTicks) {
                kept.push_back(note);
            } else {
                changed = true;
            }
        }
        notes_ = std::move(kept);
        return changed;
    }

    // -- the chromatic share (SPEC 3.8) ------------------------------------------------------------------------------

    /// What `pitch` for `note` costs against the chromatic share: 0 if allowed, 1 if it leaves scale and chord on a
    /// weak step a semitone from an allowed pitch (as `applyConstraints` accepts it within the share), -1 if it would
    /// not stand (range, bass rules or the constraint layer's snapping).
    int chromaticCost(const Note& note, int pitch) const {
        if (!in_.range.contains(pitch)) {
            return -1;
        }
        if (bass_ != nullptr && !bass_->notes.empty() &&
            violatesBassRules(*bass_, note.startTick, note.lengthTicks, pitch, tension_)) {
            return -1;
        }
        const auto chord = std::optional<Chord>(chordAt(note.startTick));
        if (isAllowed(in_.scale, keyRoot(), chord, pitch)) {
            return 0;
        }
        const auto snapped = quantize(in_.scale, keyRoot(), chord, pitch);
        const bool adjacent = snapped.has_value() && std::abs(*snapped - pitch) == 1;
        return note.startTick % kBeatTicks != 0 && adjacent ? 1 : -1;
    }

    /// True if a note of `pitch` from `start` for `length` ticks would overlap a note of that pitch (the notes that
    /// change together have the old pitch, so none of them counts).
    bool overlapsSamePitch(uint32_t start, uint32_t length, int pitch) const {
        for (size_t i = 0; i < notes_.size(); ++i) {
            const Note& other = notes_[i];
            if (other.pitch == pitch && other.startTick < start + length &&
                start < other.startTick + other.lengthTicks) {
                return true;
            }
        }
        return false;
    }

    size_t chromaticNotes() const {
        size_t used = 0;
        for (const Note& note : notes_) {
            used += isAllowed(in_.scale, keyRoot(), std::optional<Chord>(chordAt(note.startTick)), note.pitch) ? 0 : 1;
        }
        return used;
    }

    size_t chromaticBudget(size_t noteCount) const { return static_cast<size_t>(chromatic_) * noteCount / 100; }

    /// Leaves the scale where the style allows it: the share of notes is `floor(share * notes / 100)` of the voice, as
    /// in the constraint layer, and `applyConstraints` keeps every one of them. Arps, chord arps and plain stabs stay
    /// on chord tones (STYLES.md 1.11).
    void chromaticPass() {
        const std::string_view id = in_.archetypeId;
        if (chromatic_ <= 0 || notes_.empty() || id == "arp" || id == "chord_arp" || id == "stabs") {
            return;
        }
        std::sort(notes_.begin(), notes_.end(), noteLess);
        if (id == "aggro_stabs") {
            if (tension_) {
                addClusters();
            }
            return;
        }
        inflectNotes(id == "atonal_motif" || id == "sparse_hits");
    }

    /// Notes that repeat (same position in the bar and pitch) change together, so the bars keep repeating.
    /// `wished`: only the tension tones of the cell (atonal, sparse); otherwise passing and neighbour tones
    /// a semitone from the pitch, towards the next note. The notes at the extremes of the line and the last note of a
    /// phrase keep their pitch, with everything that repeats them (span, climax and ending).
    void inflectNotes(bool wished) {
        int low = 127;
        int high = 0;
        for (const Note& note : notes_) {
            low = std::min<int>(low, note.pitch);
            high = std::max<int>(high, note.pitch);
        }
        std::vector<bool> ending(notes_.size(), false);
        for (const Span& span : phrases()) {
            const uint32_t from = span.startBar * kTicksPerBar;
            const uint32_t to = from + span.lengthBars * kTicksPerBar;
            for (size_t i = notes_.size(); i-- > 0;) {
                if (notes_[i].startTick >= from && notes_[i].startTick < to) {
                    ending[i] = true;
                    break;
                }
            }
        }
        std::map<std::pair<uint32_t, int>, std::vector<size_t>> groups;
        for (size_t i = 0; i < notes_.size(); ++i) {
            const Note& note = notes_[i];
            groups[{note.startTick % kTicksPerBar, note.pitch}].push_back(i);
        }
        std::vector<std::vector<size_t>> order;
        for (auto& entry : groups) {
            const bool protectedGroup =
                !wished && (entry.first.second <= low || entry.first.second >= high ||
                            std::any_of(entry.second.begin(), entry.second.end(), [&](size_t i) { return ending[i]; }));
            if (!protectedGroup) {
                order.push_back(std::move(entry.second));
            }
        }
        const size_t budget = chromaticBudget(notes_.size());
        size_t used = chromaticNotes();
        if (used >= budget) {
            return;
        }
        rng_.shuffle(order.begin(), order.end());
        for (const std::vector<size_t>& members : order) {
            const Note& first = notes_[members.front()];
            std::array<int, 2> candidates{};
            size_t count = 0;
            if (wished) {
                const auto it = wishes_.find({first.startTick, first.pitch});
                if (it != wishes_.end()) {
                    candidates[count++] = it->second;
                }
            } else {
                const size_t index = members.front();
                const bool up = index + 1 >= notes_.size() || notes_[index + 1].pitch >= first.pitch;
                candidates[count++] = first.pitch + (up ? 1 : -1);
                candidates[count++] = first.pitch + (up ? -1 : 1);
            }
            for (size_t c = 0; c < count; ++c) {
                const int pitch = candidates[c];
                size_t cost = 0;
                bool fits = wished || (pitch >= low && pitch <= high);
                for (const size_t index : members) {
                    const int one = chromaticCost(notes_[index], pitch);
                    fits = fits && one >= 0 &&
                           !overlapsSamePitch(notes_[index].startTick, notes_[index].lengthTicks, pitch);
                    cost += one > 0 ? 1 : 0;
                }
                if (!fits || cost == 0 || used + cost > budget) {
                    continue;
                }
                for (const size_t index : members) {
                    notes_[index].pitch = static_cast<uint8_t>(pitch);
                }
                used += cost;
                break;
            }
        }
    }

    /// Aggressive stabs: a flat second above the root of the chord joins a hit where the scale lacks it (a cluster,
    /// STYLES.md 1.8), on weak steps and within the share. The added notes count as notes of the voice.
    void addClusters() {
        std::map<std::pair<uint32_t, int>, std::vector<size_t>> groups; // position in the bar, root tone of the hit
        for (size_t i = 0; i < notes_.size(); ++i) {
            const PitchClass root = rootPc(chordAt(notes_[i].startTick));
            if (pitchClassOf(notes_[i].pitch) == root) {
                groups[{notes_[i].startTick % kTicksPerBar, notes_[i].pitch}].push_back(i);
            }
        }
        std::vector<std::vector<size_t>> order;
        for (auto& entry : groups) {
            order.push_back(std::move(entry.second));
        }
        rng_.shuffle(order.begin(), order.end());
        size_t used = chromaticNotes();
        size_t total = notes_.size();
        std::vector<Note> added;
        for (const std::vector<size_t>& members : order) {
            const int pitch = notes_[members.front()].pitch + 1;
            bool fits = true;
            for (const size_t index : members) {
                const Note& hit = notes_[index];
                fits =
                    fits && chromaticCost(hit, pitch) == 1 && !overlapsSamePitch(hit.startTick, hit.lengthTicks, pitch);
            }
            if (!fits || used + members.size() > chromaticBudget(total + members.size())) {
                continue;
            }
            for (const size_t index : members) {
                Note cluster = notes_[index];
                cluster.pitch = static_cast<uint8_t>(pitch);
                added.push_back(cluster);
            }
            used += members.size();
            total += members.size();
        }
        notes_.insert(notes_.end(), added.begin(), added.end());
        std::sort(notes_.begin(), notes_.end(), noteLess);
    }

    void finish() {
        for (int round = 0; round < 8; ++round) {
            std::sort(notes_.begin(), notes_.end(), noteLess);
            bool changed = tidyPass();
            changed = pitchPass() || changed;
            changed = bassPass() || changed;
            if (!changed) {
                break;
            }
        }
        std::sort(notes_.begin(), notes_.end(), noteLess);
    }

    const MelodyInput& in_;
    Pcg32& rng_;
    uint32_t endTick_;
    int energy_;
    int creativity_;
    int chromatic_; ///< share of notes that may leave the scale, in percent (SPEC 3.8)
    bool tension_;
    std::map<std::pair<uint32_t, int>, int> wishes_; ///< (start, allowed pitch) -> tension tone outside the scale
    const Track* bass_ = nullptr;
    std::vector<Note> notes_;
};

} // namespace

std::optional<std::vector<Note>> generateMelodyNotes(const MelodyInput& input, Pcg32& rng) {
    return Melody(input, rng).run();
}

} // namespace mm::core
