#include "core/Archetype.h"

#include "core/ArchetypeMelody.h"
#include "core/KickGrid.h"
#include "core/Macros.h"
#include "core/SetProfile.h"
#include "core/Theory.h"
#include "core/VelocityContour.h"

#include <algorithm>
#include <array>
#include <map>
#include <optional>

namespace mm::core {

namespace {

constexpr uint32_t kMinNoteTicks = 60; ///< the constraint layer drops shorter notes (1/64)
constexpr uint32_t kHalfBarTicks = kTicksPerBar / 2;
constexpr int kSteps = 16; // 16th steps per bar

constexpr std::array<Archetype, 21> kArchetypes{{
    {"rolling16", VoiceRole::Bass, false, ArchetypeDensity::Dense},
    {"offbeat", VoiceRole::Bass, false, ArchetypeDensity::Neutral},
    {"gallop", VoiceRole::Bass, false, ArchetypeDensity::Neutral},
    {"rolling16_harmonic", VoiceRole::Bass, false, ArchetypeDensity::Dense},
    {"offbeat_changes", VoiceRole::Bass, false, ArchetypeDensity::Neutral},
    {"long_tied", VoiceRole::Bass, true, ArchetypeDensity::Calm},
    {"rumble", VoiceRole::Bass, false, ArchetypeDensity::Neutral},
    {"hard_offbeat", VoiceRole::Bass, false, ArchetypeDensity::Neutral},
    {"roll16_aggressive", VoiceRole::Bass, false, ArchetypeDensity::Dense},
    {"hypnotic_motif", VoiceRole::Melody, false, ArchetypeDensity::Neutral},
    {"stabs", VoiceRole::Melody, false, ArchetypeDensity::Neutral},
    {"arp", VoiceRole::Melody, false, ArchetypeDensity::Dense},
    {"acid_siren", VoiceRole::Melody, false, ArchetypeDensity::Dense},
    {"chord_arp", VoiceRole::Melody, false, ArchetypeDensity::Dense},
    {"lead_phrase", VoiceRole::Melody, false, ArchetypeDensity::Calm},
    {"call_response", VoiceRole::Melody, false, ArchetypeDensity::Neutral},
    {"pluck_seq", VoiceRole::Melody, false, ArchetypeDensity::Dense},
    {"aggro_stabs", VoiceRole::Melody, false, ArchetypeDensity::Neutral},
    {"atonal_motif", VoiceRole::Melody, false, ArchetypeDensity::Neutral},
    {"sparse_hits", VoiceRole::Melody, false, ArchetypeDensity::Calm},
    {"polymeter_seq", VoiceRole::Melody, false, ArchetypeDensity::Neutral},
}};

enum class Shape { Sixteenth, Offbeat, Gallop, Rumble, LongTied };

/// Everything an archetype needs besides its public entry.
struct Spec {
    Shape shape = Shape::Sixteenth;
    std::array<uint32_t, 2> lengths{120, 180}; ///< short and long note (cell choice); gallop: first and second note
    bool harmonic = false;                     ///< approach tones before chord changes
    bool aggressive = false;                   ///< stronger accents, octave jumps
};

Spec specOf(std::string_view id) {
    Spec spec;
    if (id == "rolling16" || id == "rolling16_harmonic") {
        spec.harmonic = id == "rolling16_harmonic";
    } else if (id == "roll16_aggressive") {
        spec.aggressive = true;
    } else if (id == "offbeat" || id == "offbeat_changes") {
        spec.shape = Shape::Offbeat;
        spec.lengths = {240, 360};
    } else if (id == "hard_offbeat") {
        spec.shape = Shape::Offbeat;
    } else if (id == "gallop") {
        spec.shape = Shape::Gallop;
        spec.lengths = {240, 180};
    } else if (id == "rumble") {
        spec.shape = Shape::Rumble;
    } else if (id == "long_tied") {
        spec.shape = Shape::LongTied;
    }
    return spec;
}

constexpr bool isOffbeatStep(int step) {
    return step % 4 == 2;
}

/// How a note takes its pitch from the chord that sounds at its start.
enum class Kind : uint8_t { Root, Fifth, Octave, Passing };

/// The decisions of one bar that repeat in every bar of the pattern (the cell). Pitches follow the chord of each bar,
/// so bars over the same chord are identical except for the variation bars.
struct Cell {
    std::array<uint8_t, kSteps> rank{}; ///< kept first = low; offbeat steps before the others
    std::array<Kind, kSteps> kind{};
    std::array<uint8_t, kSteps> pick{};
    std::array<bool, kSteps> accent{};
    std::array<bool, kSteps> longNote{};
    std::array<bool, kSteps> jump{};
    bool approach = false;
    uint8_t approachPick = 0;
};

class Generator {
public:
    Generator(const Pattern& pattern, const Archetype& archetype, const StyleProfile& style,
              const ArchetypeSettings& settings, const Range& range, const Scale& scale, Pcg32& rng)
        : pattern_(pattern), style_(style), spec_(specOf(archetype.id)), range_(range), scale_(scale), rng_(rng),
          energy_(std::clamp(settings.energyPct, 0, 100)), creativity_(std::clamp(settings.creativityPct, 0, 100)),
          profile_(settings.profile), personal_(std::clamp(settings.personalPct, 0, 100)),
          endTick_(pattern.lengthBars * kTicksPerBar), kicks_(kickTicks(pattern)),
          clearance_(style.bass.kickClearanceTicks), tension_(style.chromaticDefaultPercent >= 30) {}

    std::vector<Note> run() {
        drawCell();
        switch (spec_.shape) {
        case Shape::Rumble:
            buildRumble();
            break;
        case Shape::LongTied:
            buildLongTied();
            break;
        default:
            buildSteps();
            break;
        }
        std::sort(notes_.begin(), notes_.end(), [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
        if (spec_.shape != Shape::LongTied && spec_.shape != Shape::Rumble) {
            if (spec_.harmonic) {
                placeApproachTones();
            }
            if (style_.bass.movement.rootOnChordChange) {
                placeRootsOnChordChange();
            }
            limitChromatics();
        }
        return std::move(notes_);
    }

private:
    // -- harmony and pitch -------------------------------------------------------------------------------------

    Chord chordAt(uint32_t tick) const {
        const uint32_t halfBar = tick / kHalfBarTicks;
        for (const ChordEvent& event : pattern_.context.progression) {
            if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
                return event.chord;
            }
        }
        return Chord{};
    }

    PitchClass keyRoot() const { return pattern_.context.root; }

    int rootPitch(const Chord& chord) const {
        const auto pitchClass = static_cast<PitchClass>((keyRoot() + chord.rootOffset) % 12);
        return voiceBase(pitchClass, range_.low, range_.high).value_or(range_.low);
    }

    /// `pitch` moved by whole octaves into the range if it fell out above; the root if nothing fits.
    int fitted(int pitch, int fallback) const {
        if (pitch > range_.high && pitch - 12 >= range_.low) {
            pitch -= 12;
        }
        return range_.contains(pitch) ? pitch : fallback;
    }

    int passingPitch(const Chord& chord, uint8_t pick) const {
        const int root = rootPitch(chord);
        std::array<int, 3> offsets{chordIntervals(chord.quality)[1], 10, 1};
        std::array<int, 3> candidates{};
        size_t count = 0;
        for (size_t i = 0; i < (tension_ ? 3u : 2u); ++i) {
            const int pitch = fitted(root + offsets[i], root);
            // the flat second may leave the scale: the chromatic share decides afterwards how many stay (SPEC 3.8)
            if (pitch != root && (i == 2 || isAllowed(scale_, keyRoot(), chord, pitch))) {
                candidates[count++] = pitch;
            }
        }
        return count == 0 ? root : candidates[pick % count];
    }

    int pitchFor(Kind kind, uint8_t pick, const Chord& chord) const {
        const int root = rootPitch(chord);
        switch (kind) {
        case Kind::Fifth:
            return fitted(root + chordIntervals(chord.quality)[2], root);
        case Kind::Octave:
            return root + 12 <= range_.high ? root + 12 : root;
        case Kind::Passing:
            return passingPitch(chord, pick);
        case Kind::Root:
            break;
        }
        return root;
    }

    // -- the cell ----------------------------------------------------------------------------------------------

    /// Moves the steps that the bass lines of the reference set use more often than average towards the front of `order`
    /// (and the others back), by up to 8 places at a "Persönlich" of 100 (D-242). The order of equal keys stays; without a
    /// profile or a bass line in it nothing changes.
    void biasByProfile(std::array<int, kSteps>& order, size_t count) {
        if (profile_ == nullptr || !profile_->valid || profile_->bass.items == 0 || personal_ == 0 || count < 2) {
            return;
        }
        const auto& permille = profile_->bass.stepPermille;
        int total = 0;
        for (const int value : permille) {
            total += value;
        }
        const int mean = total / kSteps;
        std::array<int, kSteps> key{};
        for (size_t i = 0; i < count; ++i) {
            const int bias100 = (permille[static_cast<size_t>(order[i])] - mean) * personal_ * 8 / 1000;
            key[i] = static_cast<int>(i) * 100 - bias100;
        }
        std::array<size_t, kSteps> index{};
        for (size_t i = 0; i < count; ++i) {
            index[i] = i;
        }
        std::stable_sort(index.begin(), index.begin() + static_cast<std::ptrdiff_t>(count),
                         [&](size_t a, size_t b) { return key[a] < key[b]; });
        const std::array<int, kSteps> before = order;
        for (size_t i = 0; i < count; ++i) {
            order[i] = before[index[i]];
        }
    }

    void drawCell() {
        std::array<int, kSteps> offbeats{};
        std::array<int, kSteps> others{};
        size_t offbeatCount = 0;
        size_t otherCount = 0;
        for (int s = 0; s < kSteps; ++s) {
            (isOffbeatStep(s) ? offbeats[offbeatCount++] : others[otherCount++]) = s;
        }
        rng_.shuffle(offbeats.begin(), offbeats.begin() + static_cast<std::ptrdiff_t>(offbeatCount));
        rng_.shuffle(others.begin(), others.begin() + static_cast<std::ptrdiff_t>(otherCount));
        // the steps the set uses move up, by `personal_` (no draw: the shuffle is as ever); the offbeat steps are always kept
        // first and all of them, so their order never shows: only the others are moved
        biasByProfile(others, otherCount);
        uint8_t position = 0;
        for (size_t i = 0; i < offbeatCount; ++i) {
            cell_.rank[static_cast<size_t>(offbeats[i])] = position++;
        }
        for (size_t i = 0; i < otherCount; ++i) {
            cell_.rank[static_cast<size_t>(others[i])] = position++;
        }

        const BassMovement& movement = style_.bass.movement;
        const int accentChance = bassAccentChance(energy_, spec_.aggressive);
        const int jumpChance = bassJumpChance(energy_);
        for (size_t s = 0; s < kSteps; ++s) {
            const bool strong = s % 4 == 0;
            const std::array<uint32_t, 3> weights{static_cast<uint32_t>(std::max(movement.rootPercent, 0)),
                                                  static_cast<uint32_t>(std::max(movement.fifthOctavePercent, 0)),
                                                  strong ? 0u
                                                         : static_cast<uint32_t>(std::max(movement.passingPercent, 0))};
            const size_t index = rng_.weightedIndex(weights);
            Kind kind = Kind::Root;
            if (index == 1) {
                kind = rng_.bounded(2) == 0 ? Kind::Fifth : Kind::Octave;
            } else if (index == 2) {
                kind = Kind::Passing;
            }
            cell_.pick[s] = static_cast<uint8_t>(rng_.bounded(3));
            cell_.accent[s] = rng_.chance(accentChance);
            cell_.longNote[s] = rng_.chance(50);
            cell_.jump[s] = rng_.chance(jumpChance);
            if (spec_.aggressive && cell_.jump[s] && kind != Kind::Passing) {
                kind = Kind::Octave;
            }
            cell_.kind[s] = kind;
        }
        cell_.approach = rng_.chance(60);
        cell_.approachPick = static_cast<uint8_t>(rng_.bounded(3));
    }

    // -- kicks -------------------------------------------------------------------------------------------------

    bool isKick(uint32_t tick) const { return std::binary_search(kicks_.begin(), kicks_.end(), tick); }

    /// The first kick strictly after `tick`, including the first kick of the next loop pass (as the constraint layer).
    std::optional<uint32_t> nextKickAfter(uint32_t tick) const {
        const auto it = std::upper_bound(kicks_.begin(), kicks_.end(), tick);
        if (it != kicks_.end()) {
            return *it;
        }
        if (!kicks_.empty()) {
            return kicks_.front() + endTick_;
        }
        return std::nullopt;
    }

    /// End of a note that starts at `start` and would like to last `length` ticks: inside the pattern and the kick
    /// clearance before the next kick. 0 if nothing playable is left.
    uint32_t cappedLength(uint32_t start, uint32_t length) const {
        int64_t end = static_cast<int64_t>(start) + length;
        end = std::min<int64_t>(end, endTick_);
        if (const auto next = nextKickAfter(start); next.has_value()) {
            end = std::min<int64_t>(end, static_cast<int64_t>(*next) - clearance_);
        }
        const int64_t capped = end - static_cast<int64_t>(start);
        return capped >= static_cast<int64_t>(kMinNoteTicks) ? static_cast<uint32_t>(capped) : 0;
    }

    // -- rhythm shapes -----------------------------------------------------------------------------------------

    std::vector<int> stepsOfShape() const {
        switch (spec_.shape) {
        case Shape::Offbeat:
            return {2, 6, 10, 14};
        case Shape::Gallop:
            return {2, 3, 6, 7, 10, 11, 14, 15};
        default:
            break;
        }
        std::vector<int> all(kSteps);
        for (int s = 0; s < kSteps; ++s) {
            all[static_cast<size_t>(s)] = s;
        }
        return all;
    }

    /// Share of the free steps a 16th archetype plays: 40 % at energy 0 up to 100 % at energy 100, rounded up so that
    /// the density stays inside the band of the energy (D-111, D-113).
    size_t keptCount(size_t freeSteps) const { return energyKeptSteps(freeSteps, energy_); }

    void buildSteps() {
        const std::vector<int> shape = stepsOfShape();
        const bool sixteenth = spec_.shape == Shape::Sixteenth;
        for (uint32_t bar = 0; bar < pattern_.lengthBars; ++bar) {
            const uint32_t barStart = bar * kTicksPerBar;
            std::vector<int> domain;
            for (const int s : shape) {
                if (!isKick(barStart + static_cast<uint32_t>(s) * kTicksPerStep)) {
                    domain.push_back(s);
                }
            }
            std::vector<int> kept = domain;
            if (sixteenth) {
                std::sort(kept.begin(), kept.end(), [&](int a, int b) {
                    return cell_.rank[static_cast<size_t>(a)] < cell_.rank[static_cast<size_t>(b)];
                });
                kept.resize(std::min(kept.size(), keptCount(domain.size())));
            }
            const bool variationBar = pattern_.lengthBars >= 4 && bar % 4 == 3;
            if (variationBar && rng_.chance(variationChance(creativity_, energy_)) && !domain.empty()) {
                varyBar(domain, kept, sixteenth);
            }
            for (const int s : domain) {
                if (std::find(kept.begin(), kept.end(), s) != kept.end()) {
                    addStepNote(barStart, s);
                }
            }
        }
    }

    /// The variation of a bar: one note moves to a free step of the shape, so the density stays; without a free step
    /// (full density, fixed shapes) one note is dropped.
    void varyBar(const std::vector<int>& domain, std::vector<int>& kept, bool movable) {
        std::vector<int> absent;
        for (const int s : domain) {
            if (std::find(kept.begin(), kept.end(), s) == kept.end()) {
                absent.push_back(s);
            }
        }
        if (kept.empty()) {
            return;
        }
        kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(rng_.bounded(static_cast<uint32_t>(kept.size()))));
        if (movable && !absent.empty()) {
            kept.push_back(absent[rng_.bounded(static_cast<uint32_t>(absent.size()))]);
        }
    }

    void addStepNote(uint32_t barStart, int step) {
        const auto index = static_cast<size_t>(step);
        const uint32_t start = barStart + static_cast<uint32_t>(step) * kTicksPerStep;
        uint32_t wanted = spec_.lengths[cell_.longNote[index] ? 1 : 0];
        if (spec_.shape == Shape::Gallop) {
            wanted = spec_.lengths[step % 2 == 0 ? 0 : 1];
        }
        const uint32_t length = cappedLength(start, wanted);
        if (length == 0) {
            return;
        }
        Note note;
        note.startTick = start;
        note.lengthTicks = length;
        note.pitch = static_cast<uint8_t>(pitchFor(cell_.kind[index], cell_.pick[index], chordAt(start)));
        note.accent = spec_.shape == Shape::Sixteenth && isOffbeatStep(step) && cell_.accent[index];
        notes_.push_back(note);
    }

    void buildRumble() {
        for (const uint32_t kick : kicks_) {
            const uint32_t start = kick + kTicksPerStep;
            if (start >= endTick_ || isKick(start)) {
                continue;
            }
            const uint32_t length = cappedLength(start, endTick_ - start);
            if (length == 0) {
                continue;
            }
            Note note;
            note.startTick = start;
            note.lengthTicks = length;
            note.pitch = static_cast<uint8_t>(rootPitch(chordAt(start)));
            notes_.push_back(note);
        }
    }

    void buildLongTied() {
        for (uint32_t bar = 0; bar < pattern_.lengthBars; ++bar) {
            const uint32_t barStart = bar * kTicksPerBar;
            const Chord first = chordAt(barStart);
            const Chord second = chordAt(barStart + kHalfBarTicks);
            const bool split = !(first == second);
            const bool variationBar = pattern_.lengthBars >= 4 && bar % 4 == 3;
            const bool vary = variationBar && rng_.chance(variationChance(creativity_, energy_));
            if (split || vary) {
                addLongNote(barStart, kHalfBarTicks, rootPitch(first));
                int pitch = rootPitch(second);
                if (!split) {
                    pitch = pitchFor(rng_.bounded(2) == 0 ? Kind::Fifth : Kind::Octave, 0, second);
                }
                addLongNote(barStart + kHalfBarTicks, kHalfBarTicks, pitch);
            } else {
                addLongNote(barStart, kTicksPerBar, rootPitch(first));
            }
        }
        // Slides between different pitches (never onto the same pitch, which would be a tie, nor off the last note).
        for (size_t i = 0; i + 1 < notes_.size(); ++i) {
            if (notes_[i].pitch != notes_[i + 1].pitch) {
                notes_[i].slide = rng_.chance(50);
            }
        }
    }

    void addLongNote(uint32_t start, uint32_t length, int pitch) {
        Note note;
        note.startTick = start;
        note.lengthTicks = length;
        note.pitch = static_cast<uint8_t>(pitch);
        notes_.push_back(note);
    }

    // -- chord changes -----------------------------------------------------------------------------------------

    /// Start ticks of the chords that differ from the one before (and the pattern start).
    std::vector<uint32_t> chordChanges() const {
        std::vector<uint32_t> ticks;
        std::optional<Chord> previous;
        for (uint32_t tick = 0; tick < endTick_; tick += kHalfBarTicks) {
            const Chord chord = chordAt(tick);
            if (!previous.has_value() || !(*previous == chord)) {
                ticks.push_back(tick);
            }
            previous = chord;
        }
        return ticks;
    }

    /// The last note shortly before a chord change moves to a scale tone next to the new root (Melodic Techno).
    void placeApproachTones() {
        if (!cell_.approach) {
            return;
        }
        for (const uint32_t change : chordChanges()) {
            if (change == 0) {
                continue;
            }
            Note* last = nullptr;
            for (Note& note : notes_) {
                if (note.startTick < change && note.startTick + kTicksPerBeat >= change) {
                    last = &note;
                }
            }
            if (last == nullptr) {
                continue;
            }
            const int target = rootPitch(chordAt(change));
            const std::array<int, 3> candidates{target - 1, target - 2, target + 1};
            const Chord here = chordAt(last->startTick);
            for (size_t i = 0; i < candidates.size(); ++i) {
                const int pitch = candidates[(i + cell_.approachPick) % candidates.size()];
                if (range_.contains(pitch) && isAllowed(scale_, keyRoot(), here, pitch)) {
                    last->pitch = static_cast<uint8_t>(pitch);
                    break;
                }
            }
        }
    }

    /// Melodic Techno: the first note of every new chord is its root.
    void placeRootsOnChordChange() {
        const std::vector<uint32_t> changes = chordChanges();
        for (size_t i = 0; i < changes.size(); ++i) {
            const uint32_t end = i + 1 < changes.size() ? changes[i + 1] : endTick_;
            for (Note& note : notes_) {
                if (note.startTick >= changes[i] && note.startTick < end) {
                    note.pitch = static_cast<uint8_t>(rootPitch(chordAt(changes[i])));
                    break;
                }
            }
        }
    }

    /// Passing tones outside the scale (the flat second, Hard/Industrial) stay within the chromatic share of the style,
    /// `floor(share * notes / 100)`, as the constraint layer enforces it. Notes that repeat (same position in the bar
    /// and pitch) stay or go together; the others fall back to the root of their chord.
    void limitChromatics() {
        std::map<std::pair<uint32_t, int>, std::vector<size_t>> groups;
        for (size_t i = 0; i < notes_.size(); ++i) {
            const Note& note = notes_[i];
            if (!isAllowed(scale_, keyRoot(), chordAt(note.startTick), note.pitch)) {
                groups[{note.startTick % kTicksPerBar, note.pitch}].push_back(i);
            }
        }
        if (groups.empty()) {
            return;
        }
        std::vector<std::vector<size_t>> order;
        for (auto& entry : groups) {
            order.push_back(std::move(entry.second));
        }
        rng_.shuffle(order.begin(), order.end());
        const size_t budget = chromaticFill(
            static_cast<size_t>(std::max(style_.chromaticDefaultPercent, 0)) * notes_.size() / 100, creativity_);
        size_t used = 0;
        for (const std::vector<size_t>& members : order) {
            if (used + members.size() <= budget) {
                used += members.size();
                continue;
            }
            for (const size_t index : members) {
                notes_[index].pitch = static_cast<uint8_t>(rootPitch(chordAt(notes_[index].startTick)));
            }
        }
    }

    static constexpr uint32_t kTicksPerBeat = 960;

    const Pattern& pattern_;
    const StyleProfile& style_;
    Spec spec_;
    Range range_;
    const Scale& scale_;
    Pcg32& rng_;
    int energy_;
    int creativity_;
    const SetProfile* profile_;
    int personal_;
    uint32_t endTick_;
    std::vector<uint32_t> kicks_;
    uint32_t clearance_;
    bool tension_;
    Cell cell_;
    std::vector<Note> notes_;
};

} // namespace

std::span<const Archetype> allArchetypes() {
    return kArchetypes;
}

const Archetype* findArchetype(std::string_view id) {
    for (const Archetype& archetype : kArchetypes) {
        if (archetype.id == id) {
            return &archetype;
        }
    }
    return nullptr;
}

bool archetypeFitsRole(const Archetype& archetype, VoiceRole role) {
    return isBassRole(role) ? archetype.role == VoiceRole::Bass : archetype.role == VoiceRole::Melody;
}

namespace {

/// The archetypes a role draws from by itself (D-250); a musician may still choose any other melodic one by hand.
/// `Melody` takes every melodic archetype, as before.
constexpr std::array<std::string_view, 2> kStabsFamily{"stabs", "aggro_stabs"};
constexpr std::array<std::string_view, 3> kArpFamily{"arp", "chord_arp", "pluck_seq"};
constexpr std::array<std::string_view, 4> kCounterFamily{"call_response", "sparse_hits", "lead_phrase",
                                                         "hypnotic_motif"};

template <size_t N> bool inFamily(const std::array<std::string_view, N>& family, std::string_view id) {
    return std::find(family.begin(), family.end(), id) != family.end();
}

bool inRoleFamily(VoiceRole role, std::string_view id) {
    switch (role) {
    case VoiceRole::Stabs:
        return inFamily(kStabsFamily, id);
    case VoiceRole::Arp:
        return inFamily(kArpFamily, id);
    case VoiceRole::Counter:
        return inFamily(kCounterFamily, id);
    case VoiceRole::Bass:
    case VoiceRole::Melody:
        return true;
    }
    return true;
}

/// The style's list for the role: its own when it has one (D-251), else the melody list.
const std::vector<WeightedId>& roleArchetypes(const StyleProfile& style, VoiceRole role) {
    const MelodyProfile& melody = style.melody;
    switch (role) {
    case VoiceRole::Bass:
        return style.bass.archetypes;
    case VoiceRole::Stabs:
        return melody.stabsArchetypes.empty() ? melody.archetypes : melody.stabsArchetypes;
    case VoiceRole::Arp:
        return melody.arpArchetypes.empty() ? melody.archetypes : melody.arpArchetypes;
    case VoiceRole::Counter:
        return melody.counterArchetypes.empty() ? melody.archetypes : melody.counterArchetypes;
    case VoiceRole::Melody:
        return melody.archetypes;
    }
    return melody.archetypes;
}

} // namespace

std::string chooseArchetype(const StyleProfile& style, VoiceRole role, int energyPct, Pcg32& rng, int creativityPct) {
    const int energy = std::clamp(energyPct, 0, 100);
    const std::vector<WeightedId>& entries = roleArchetypes(style, role);
    std::vector<std::string> ids;
    std::vector<uint32_t> weights;
    const int range = archetypeScatterRange(creativityPct);
    for (const WeightedId& entry : entries) {
        const Archetype* archetype = findArchetype(entry.id);
        if (archetype == nullptr || !archetypeFitsRole(*archetype, role) || !inRoleFamily(role, entry.id) ||
            entry.weight <= 0) {
            continue;
        }
        const int factor = archetypeDensityFactor(archetype->density == ArchetypeDensity::Dense,
                                                  archetype->density == ArchetypeDensity::Calm, energy);
        ids.push_back(entry.id);
        weights.push_back(static_cast<uint32_t>(entry.weight) * static_cast<uint32_t>(factor) *
                          static_cast<uint32_t>(archetypeScatterFactor(range, rng)));
    }
    if (ids.empty() && role != VoiceRole::Bass && role != VoiceRole::Melody) {
        // the style names none of this role's archetypes: they all count the same (D-250)
        for (const Archetype& archetype : allArchetypes()) {
            if (inRoleFamily(role, archetype.id)) {
                ids.emplace_back(archetype.id);
                weights.push_back(1);
            }
        }
    }
    const size_t index = rng.weightedIndex(weights);
    return index < ids.size() ? ids[index] : std::string();
}

std::string resolveArchetype(const Track& track, const StyleProfile& style, int energyPct, Pcg32& rng,
                             int creativityPct) {
    if (!track.archetypeAuto) {
        const Archetype* manual = findArchetype(track.archetypeId);
        if (manual != nullptr && archetypeFitsRole(*manual, track.role)) {
            return track.archetypeId;
        }
    }
    return chooseArchetype(style, track.role, energyPct, rng, creativityPct);
}

bool generateVoice(Pattern& pattern, size_t voiceIndex, std::string_view archetypeId, const StyleProfile& style,
                   const ArchetypeSettings& settings, Pcg32& rng) {
    const Archetype* archetype = findArchetype(archetypeId);
    if (archetype == nullptr || voiceIndex >= pattern.voices.size() ||
        !archetypeFitsRole(*archetype, pattern.voices[voiceIndex].role)) {
        return false;
    }
    Track& track = pattern.voices[voiceIndex];
    const Scale* scale = findScale(pattern.context.scaleId);
    const auto range =
        effectiveRange(style.registerProfile(), track.role, archetype->id, pattern.voicing, track.octaveOffset);
    if (scale == nullptr || !range.has_value()) {
        return false;
    }
    std::vector<Note> notes;
    if (archetype->role == VoiceRole::Bass) {
        notes = Generator(pattern, *archetype, style, settings, *range, *scale, rng).run();
    } else {
        const MelodyInput input{
            pattern, style, archetype->id, *range, *scale, settings.energyPct, settings.creativityPct, settings.profile,
            settings.personalPct};
        auto melody = generateMelodyNotes(input, rng);
        if (!melody.has_value()) {
            return false;
        }
        notes = std::move(*melody);
    }
    track.notes.clear();
    for (Note& note : notes) {
        note.id = allocateNoteId(pattern);
        track.notes.push_back(note);
    }
    track.archetypeId = std::string(archetype->id);
    // The contour sets the base velocity; a note keeps its offset from 100 (micro variations of motifs).
    const int energy = std::clamp(settings.energyPct, 0, 100);
    const VelocityContour contour = applyEnergy(archetypeContour(archetype->id), static_cast<uint16_t>(energy * 10));
    for (Note& note : track.notes) {
        note.velocity =
            static_cast<uint8_t>(std::clamp(contour.velocityAtTick(note.startTick) + (note.velocity - 100), 1, 127));
    }
    return true;
}

VelocityContour archetypeContour(std::string_view archetypeId) {
    VelocityContour contour;
    if (archetypeId == "hard_offbeat") {
        contour.steps.fill(115);
    } else if (archetypeId == "roll16_aggressive") {
        contour.steps.fill(110);
    } else if (archetypeId == "stabs") {
        contour.steps.fill(105);
    } else if (archetypeId == "aggro_stabs") {
        contour.steps.fill(112);
    } else if (archetypeId == "gallop") {
        for (size_t s = 0; s < kContourSteps; ++s) {
            contour.steps[s] = s % 2 == 0 ? 100 : 80; // the second note of every pair is quieter
        }
    }
    return contour;
}

StyleProfile withChromatic(const StyleProfile& style, int chromaticPct) {
    StyleProfile copy = style;
    if (chromaticPct >= 0) {
        copy.chromaticDefaultPercent = std::min(chromaticPct, 100);
    }
    return copy;
}

namespace {

/// The chromatic share a pattern works with: the one chosen for it, else the style's.
int chromaticOf(const Pattern& pattern, const StyleProfile& style) {
    return pattern.info.chromaticPct >= 0 ? pattern.info.chromaticPct : style.chromaticDefaultPercent;
}

} // namespace

ConstraintSettings constraintSettingsFor(const Pattern& pattern, const StyleProfile& style) {
    ConstraintSettings settings = ConstraintSettings::forPattern(pattern, style.registerProfile());
    settings.kickClearanceTicks = style.bass.kickClearanceTicks;
    settings.chromaticPercent = chromaticOf(pattern, style);
    return settings;
}

QualityContext qualityContextFor(const Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings) {
    QualityContext context;
    context.energyPct = settings.energyPct;
    context.creativityPct = settings.creativityPct;
    context.chromaticPercent = chromaticOf(pattern, style);
    context.profile = settings.profile;
    context.personalPct = settings.personalPct;
    for (const Track& track : pattern.voices) {
        context.ignoresKick.push_back(ignoresKickArchetype(track.archetypeId));
    }
    return context;
}

} // namespace mm::core
