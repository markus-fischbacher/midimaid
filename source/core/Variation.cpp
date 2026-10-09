#include "core/Variation.h"

#include "core/Random.h"
#include "core/Theory.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>
#include <set>

namespace mm::core {

namespace {

constexpr uint32_t kLengthStepTicks = 60; // 1/64
constexpr uint32_t kHalfBarTicks = kTicksPerBar / 2;
constexpr uint32_t kGridTicks = 240; // 1/16

enum class SubtleOperator { Length, VelocityContour, MoveAccent, ReplaceNote };

std::optional<Chord> chordAt(const HarmonicContext& harmony, uint32_t tick) {
    const uint32_t halfBar = tick / kHalfBarTicks;
    for (const ChordEvent& event : harmony.progression) {
        if (halfBar >= event.startHalfBar && halfBar < event.startHalfBar + event.lengthHalfBars) {
            return event.chord;
        }
    }
    return std::nullopt;
}

/// Longest length note `index` may have: up to the next note of the voice or the pattern end.
uint32_t roomAfter(const std::vector<Note>& notes, size_t index, const VariationVoice& context) {
    const uint32_t start = notes[index].startTick;
    uint32_t limit = context.patternEnd;
    if (index + 1 < notes.size()) {
        limit = std::min(limit, notes[index + 1].startTick);
    }
    return limit > start ? limit - start : 0;
}

bool changeLength(std::vector<Note>& notes, int strengthPct, const VariationVoice& context, Pcg32& rng) {
    const size_t index = rng.bounded(static_cast<uint32_t>(notes.size()));
    Note& note = notes[index];
    if (note.lock.rhythm || note.slide) {
        return false;
    }
    const auto maxSteps = static_cast<uint32_t>(1 + strengthPct / 34); // 1 to 3 sixty-fourths
    const uint32_t delta = (1 + rng.bounded(maxSteps)) * kLengthStepTicks;
    const bool longer = rng.chance(50);
    const uint32_t room = roomAfter(notes, index, context);
    uint32_t length = note.lengthTicks;
    if (longer) {
        length = std::min(note.lengthTicks + delta, room);
    } else if (note.lengthTicks > delta + kLengthStepTicks) {
        length = note.lengthTicks - delta;
    }
    if (length == note.lengthTicks || length < kLengthStepTicks) {
        return false;
    }
    note.lengthTicks = length;
    return true;
}

bool shapeVelocity(std::vector<Note>& notes, int strengthPct, Pcg32& rng) {
    const auto window = static_cast<size_t>(rng.range(2, 4));
    const size_t first = rng.bounded(static_cast<uint32_t>(notes.size()));
    const int amplitude = 3 + strengthPct * 17 / 100; // 3 to 20
    const int direction = rng.chance(50) ? 1 : -1;
    bool changed = false;
    for (size_t j = 0; j < window && first + j < notes.size(); ++j) {
        Note& note = notes[first + j];
        if (note.lock.velocity) {
            continue;
        }
        const int shift = direction * amplitude * static_cast<int>(j + 1) / static_cast<int>(window);
        const int velocity = std::clamp(static_cast<int>(note.velocity) + shift, 1, 127);
        if (velocity != note.velocity) {
            note.velocity = static_cast<uint8_t>(velocity);
            changed = true;
        }
    }
    return changed;
}

bool moveAccent(std::vector<Note>& notes, Pcg32& rng) {
    std::vector<size_t> accented;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].accent && !notes[i].lock.velocity) {
            accented.push_back(i);
        }
    }
    if (accented.empty()) {
        return false;
    }
    const size_t from = accented[rng.bounded(static_cast<uint32_t>(accented.size()))];
    std::vector<size_t> targets;
    for (int d = -2; d <= 2; ++d) {
        const auto to = static_cast<long>(from) + d;
        if (d != 0 && to >= 0 && to < static_cast<long>(notes.size()) && !notes[static_cast<size_t>(to)].accent &&
            !notes[static_cast<size_t>(to)].lock.velocity) {
            targets.push_back(static_cast<size_t>(to));
        }
    }
    if (targets.empty()) {
        return false;
    }
    const size_t to = targets[rng.bounded(static_cast<uint32_t>(targets.size()))];
    notes[from].accent = false;
    notes[to].accent = true;
    return true;
}

bool replaceNote(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    // Weak steps (off the beat) are chosen three times as often as notes on the beat.
    std::vector<uint32_t> weights;
    weights.reserve(notes.size());
    for (const Note& note : notes) {
        // A note that was replaced already stays: a replacement is one step to a neighbour, never a drift.
        const bool replaced =
            notes.size() == context.original->size() && (*context.original)[weights.size()].pitch != note.pitch;
        weights.push_back(note.lock.pitch || replaced ? 0u : (note.startTick % 960 != 0 ? 3u : 1u));
    }
    const size_t index = rng.weightedIndex(weights);
    if (index >= notes.size()) {
        return false;
    }
    Note& note = notes[index];
    const auto chord = chordAt(*context.harmony, note.startTick);
    std::vector<int> candidates;
    for (const int offset : {-2, -1, 1, 2}) {
        const int pitch = static_cast<int>(note.pitch) + offset;
        if (pitch >= context.rangeLow && pitch <= context.rangeHigh &&
            isAllowed(*context.scale, context.harmony->root, chord, pitch)) {
            candidates.push_back(pitch);
        }
    }
    if (candidates.empty()) {
        return false;
    }
    note.pitch = static_cast<uint8_t>(candidates[rng.bounded(static_cast<uint32_t>(candidates.size()))]);
    return true;
}

bool applyOperator(SubtleOperator op, std::vector<Note>& notes, const LockFlags& lock, int strengthPct,
                   const VariationVoice& context, Pcg32& rng) {
    switch (op) {
    case SubtleOperator::Length:
        return !lock.rhythm && changeLength(notes, strengthPct, context, rng);
    case SubtleOperator::VelocityContour:
        return !lock.velocity && shapeVelocity(notes, strengthPct, rng);
    case SubtleOperator::MoveAccent:
        return !lock.velocity && moveAccent(notes, rng);
    case SubtleOperator::ReplaceNote:
        return !lock.pitch && replaceNote(notes, context, rng);
    }
    return false;
}

constexpr std::array<SubtleOperator, 4> kSubtleOperators = {SubtleOperator::Length, SubtleOperator::VelocityContour,
                                                            SubtleOperator::MoveAccent, SubtleOperator::ReplaceNote};

void sortByStart(std::vector<Note>& notes) {
    std::stable_sort(notes.begin(), notes.end(),
                     [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
}

bool startTaken(const std::vector<Note>& notes, uint32_t tick, size_t except) {
    for (size_t i = 0; i < notes.size(); ++i) {
        if (i != except && notes[i].startTick == tick) {
            return true;
        }
    }
    return false;
}

/// Shortens every note that reaches into its successor. False (nothing guaranteed) when that would hit a note whose
/// rhythm is locked or leave a note shorter than 1/64.
bool trimOverlaps(std::vector<Note>& notes, const std::set<uint32_t>& frozenIds) {
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
        const uint32_t limit = notes[i + 1].startTick;
        if (notes[i].startTick + notes[i].lengthTicks > limit) {
            if (limit - notes[i].startTick < kLengthStepTicks || frozenIds.count(notes[i].id) != 0) {
                return false;
            }
            notes[i].lengthTicks = limit - notes[i].startTick;
        }
    }
    return true;
}

std::set<uint32_t> rhythmLockedIds(const std::vector<Note>& notes) {
    std::set<uint32_t> ids;
    for (const Note& note : notes) {
        if (note.lock.rhythm) {
            ids.insert(note.id);
        }
    }
    return ids;
}

} // namespace

bool shiftRhythm(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    std::vector<Note> trial = notes;
    const int moves = rng.range(1, 3);
    std::set<uint32_t> shifted;
    bool moved = false;
    for (int m = 0; m < moves; ++m) {
        const size_t index = rng.bounded(static_cast<uint32_t>(trial.size()));
        Note& note = trial[index];
        if (note.lock.rhythm || !shifted.insert(note.id).second) { // a note moves once: no net zero, no double step
            continue;
        }
        const bool later = rng.chance(50);
        if ((!later && note.startTick < kGridTicks) || (later && note.startTick + kGridTicks >= context.patternEnd)) {
            continue;
        }
        const uint32_t start = later ? note.startTick + kGridTicks : note.startTick - kGridTicks;
        if (startTaken(trial, start, index)) {
            continue;
        }
        note.startTick = start;
        moved = true;
    }
    if (!moved) {
        return false;
    }
    sortByStart(trial);
    // The moved note ends before its successor and the predecessor before it, and nothing leaves the pattern.
    const auto frozen = rhythmLockedIds(trial);
    if (!trimOverlaps(trial, frozen)) {
        return false;
    }
    for (const Note& note : trial) {
        if (note.startTick + note.lengthTicks > context.patternEnd) {
            return false;
        }
    }
    notes = std::move(trial);
    return true;
}

namespace {

std::optional<Chord> sameChordAt(const VariationVoice& context, uint32_t tick) {
    return chordAt(*context.harmony, tick);
}

} // namespace

bool swapTones(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    if (notes.size() < 2) {
        return false;
    }
    if (rng.chance(50)) {
        // Two notes under the same chord trade their pitches.
        const size_t a = rng.bounded(static_cast<uint32_t>(notes.size()));
        std::vector<size_t> partners;
        for (size_t b = 0; b < notes.size(); ++b) {
            if (b != a && notes[b].pitch != notes[a].pitch && !notes[b].lock.pitch &&
                sameChordAt(context, notes[b].startTick) == sameChordAt(context, notes[a].startTick)) {
                partners.push_back(b);
            }
        }
        if (!notes[a].lock.pitch && !partners.empty()) {
            const size_t b = partners[rng.bounded(static_cast<uint32_t>(partners.size()))];
            std::swap(notes[a].pitch, notes[b].pitch);
            return true;
        }
    }
    // One note jumps to another allowed pitch, further away than the subtle replacement reaches.
    std::vector<uint32_t> weights;
    for (const Note& note : notes) {
        weights.push_back(note.lock.pitch ? 0u : 1u);
    }
    const size_t index = rng.weightedIndex(weights);
    if (index >= notes.size()) {
        return false;
    }
    Note& note = notes[index];
    const auto chord = chordAt(*context.harmony, note.startTick);
    std::vector<int> candidates;
    for (int offset = -9; offset <= 9; ++offset) {
        const int pitch = static_cast<int>(note.pitch) + offset;
        if (std::abs(offset) >= 3 && pitch >= context.rangeLow && pitch <= context.rangeHigh &&
            isAllowed(*context.scale, context.harmony->root, chord, pitch)) {
            candidates.push_back(pitch);
        }
    }
    if (candidates.empty()) {
        return false;
    }
    note.pitch = static_cast<uint8_t>(candidates[rng.bounded(static_cast<uint32_t>(candidates.size()))]);
    return true;
}

namespace {} // namespace

bool octaveJump(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    std::vector<uint32_t> weights;
    for (const Note& note : notes) {
        weights.push_back(note.lock.pitch ? 0u : 1u);
    }
    const size_t index = rng.weightedIndex(weights);
    if (index >= notes.size()) {
        return false;
    }
    Note& note = notes[index];
    const int first = rng.chance(50) ? 12 : -12;
    for (const int jump : {first, -first}) {
        const int pitch = static_cast<int>(note.pitch) + jump;
        if (pitch >= context.rangeLow && pitch <= context.rangeHigh) {
            note.pitch = static_cast<uint8_t>(pitch);
            return true;
        }
    }
    return false;
}

namespace {

bool addNoteToVoice(std::vector<Note>& notes, Pattern& pattern, const VariationVoice& context, Pcg32& rng) {
    std::vector<uint32_t> slots;
    std::vector<uint32_t> weights;
    for (uint32_t tick = 0; tick < context.patternEnd; tick += kGridTicks) {
        if (startTaken(notes, tick, notes.size())) {
            continue;
        }
        const auto next = std::find_if(notes.begin(), notes.end(), [&](const Note& n) { return n.startTick > tick; });
        const auto prev = std::find_if(notes.rbegin(), notes.rend(), [&](const Note& n) { return n.startTick < tick; });
        // A predecessor that reaches over the slot is shortened, unless its rhythm is locked.
        if (prev != notes.rend() && prev->startTick + prev->lengthTicks > tick && prev->lock.rhythm) {
            continue;
        }
        const uint32_t limit = next != notes.end() ? next->startTick : context.patternEnd;
        if (limit - tick < kLengthStepTicks) {
            continue;
        }
        slots.push_back(tick);
        weights.push_back(tick % 960 != 0 ? 3u : 1u); // weak steps first
    }
    const size_t pick = rng.weightedIndex(weights);
    if (pick >= slots.size()) {
        return false;
    }
    const uint32_t tick = slots[pick];
    const auto prev = std::find_if(notes.rbegin(), notes.rend(), [&](const Note& n) { return n.startTick < tick; });
    const Note& model = prev != notes.rend() ? *prev : notes.front();
    const auto chord = chordAt(*context.harmony, tick);
    std::vector<int> candidates;
    for (const int offset : {0, -2, -1, 1, 2}) {
        const int pitch = static_cast<int>(model.pitch) + offset;
        if (pitch >= context.rangeLow && pitch <= context.rangeHigh &&
            isAllowed(*context.scale, context.harmony->root, chord, pitch)) {
            candidates.push_back(pitch);
        }
    }
    if (candidates.empty()) {
        return false;
    }
    Note added;
    added.id = allocateNoteId(pattern);
    added.pitch = static_cast<uint8_t>(candidates[rng.bounded(static_cast<uint32_t>(candidates.size()))]);
    added.startTick = tick;
    added.velocity = model.velocity;
    const auto next = std::find_if(notes.begin(), notes.end(), [&](const Note& n) { return n.startTick > tick; });
    const uint32_t limit = next != notes.end() ? next->startTick : context.patternEnd;
    added.lengthTicks = std::min(kGridTicks, limit - tick);
    for (auto it = notes.rbegin(); it != notes.rend(); ++it) {
        if (it->startTick < tick) {
            if (it->startTick + it->lengthTicks > tick) {
                it->lengthTicks = tick - it->startTick;
            }
            break;
        }
    }
    notes.push_back(added);
    sortByStart(notes);
    return true;
}

bool removeNoteFromVoice(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    if (notes.size() <= 2) {
        return false;
    }
    std::vector<uint32_t> weights;
    for (const Note& note : notes) {
        const bool onBeat = note.startTick % 960 == 0;
        // The bass keeps its notes on the beat; accents stay; locked notes stay.
        const bool keep = note.lock.rhythm || note.accent || (context.role == VoiceRole::Bass && onBeat);
        weights.push_back(keep ? 0u : (onBeat ? 1u : 3u));
    }
    const size_t index = rng.weightedIndex(weights);
    if (index >= notes.size()) {
        return false;
    }
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

} // namespace

bool changeDensity(std::vector<Note>& notes, Pattern& pattern, const LockFlags& lock, const VariationVoice& context,
                   Pcg32& rng) {
    if (lock.rhythm) {
        return false;
    }
    const bool more = rng.chance(50);
    const int count = rng.range(1, 2);
    bool changed = false;
    for (int i = 0; i < count; ++i) {
        changed =
            (more ? addNoteToVoice(notes, pattern, context, rng) : removeNoteFromVoice(notes, context, rng)) || changed;
    }
    return changed;
}

namespace {

/// Notes of `notes` that start in [from, to), as indices.
std::vector<size_t> notesIn(const std::vector<Note>& notes, uint32_t from, uint32_t to) {
    std::vector<size_t> found;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].startTick >= from && notes[i].startTick < to) {
            found.push_back(i);
        }
    }
    return found;
}

} // namespace

bool invertOrMirror(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    // One bar or one phrase.
    uint32_t from = 0;
    uint32_t to = context.patternEnd;
    if (rng.chance(50)) {
        const uint32_t bars = context.patternEnd / kTicksPerBar;
        from = rng.bounded(bars) * kTicksPerBar;
        to = from + kTicksPerBar;
    } else if (context.phrases != nullptr && !context.phrases->empty()) {
        const Phrase& phrase = (*context.phrases)[rng.bounded(static_cast<uint32_t>(context.phrases->size()))];
        from = phrase.startBar * kTicksPerBar;
        to = from + phrase.lengthBars * kTicksPerBar;
    }
    const auto segment = notesIn(notes, from, to);
    if (segment.size() < 2) {
        return false;
    }
    for (const size_t i : segment) {
        if (notes[i].lock.pitch) {
            return false;
        }
    }
    std::vector<int> pitches;
    if (rng.chance(50)) {
        // Mirror around the first note, snapped back to scale or chord and folded into the range.
        const int pivot = notes[segment.front()].pitch;
        for (const size_t i : segment) {
            const auto chord = chordAt(*context.harmony, notes[i].startTick);
            const auto snapped = quantize(*context.scale, context.harmony->root, chord, 2 * pivot - notes[i].pitch);
            if (!snapped) {
                return false;
            }
            int pitch = *snapped;
            while (pitch < context.rangeLow) {
                pitch += 12;
            }
            while (pitch > context.rangeHigh) {
                pitch -= 12;
            }
            if (pitch < context.rangeLow) {
                return false;
            }
            pitches.push_back(pitch);
        }
    } else {
        // Retrograde: the pitches run backwards over the same rhythm.
        for (auto it = segment.rbegin(); it != segment.rend(); ++it) {
            pitches.push_back(notes[*it].pitch);
        }
    }
    bool changed = false;
    for (size_t k = 0; k < segment.size(); ++k) {
        changed = changed || notes[segment[k]].pitch != pitches[k];
        notes[segment[k]].pitch = static_cast<uint8_t>(pitches[k]);
    }
    return changed;
}

namespace {} // namespace

bool swapCallAndResponse(std::vector<Note>& notes, const VariationVoice& context, Pcg32& rng) {
    const uint32_t pairs = context.patternEnd / kTicksPerBar / 2;
    if (pairs == 0) {
        return false;
    }
    const uint32_t from = rng.bounded(pairs) * 2 * kTicksPerBar;
    const auto call = notesIn(notes, from, from + kTicksPerBar);
    const auto response = notesIn(notes, from + kTicksPerBar, from + 2 * kTicksPerBar);
    if (call.empty() && response.empty()) {
        return false;
    }
    for (const auto* group : {&call, &response}) {
        for (const size_t i : *group) {
            if (notes[i].lock.pitch || notes[i].lock.rhythm) {
                return false;
            }
        }
    }
    std::vector<Note> trial = notes;
    for (const size_t i : call) {
        trial[i].startTick += kTicksPerBar;
    }
    for (const size_t i : response) {
        trial[i].startTick -= kTicksPerBar;
    }
    sortByStart(trial);
    // Equal content in both bars (same pitches, lengths and offsets) is no change.
    std::vector<Note> a;
    std::vector<Note> b;
    for (const size_t i : call) {
        a.push_back(notes[i]);
        a.back().id = 0;
        a.back().startTick -= from;
    }
    for (const size_t i : response) {
        b.push_back(notes[i]);
        b.back().id = 0;
        b.back().startTick -= from + kTicksPerBar;
    }
    if (a == b) {
        return false;
    }
    notes = std::move(trial);
    return true;
}

namespace {} // namespace

bool applyStructural(StructuralOperator op, std::vector<Note>& notes, Pattern& pattern, const LockFlags& lock,
                     const VariationVoice& context, Pcg32& rng) {
    switch (op) {
    case StructuralOperator::ShiftRhythm:
        return !lock.rhythm && shiftRhythm(notes, context, rng);
    case StructuralOperator::SwapTones:
        return !lock.pitch && swapTones(notes, context, rng);
    case StructuralOperator::OctaveJump:
        return !lock.pitch && octaveJump(notes, context, rng);
    case StructuralOperator::Density:
        return changeDensity(notes, pattern, lock, context, rng);
    case StructuralOperator::InvertOrMirror:
        return context.role == VoiceRole::Melody && !lock.pitch && invertOrMirror(notes, context, rng);
    case StructuralOperator::CallResponse:
        return context.role == VoiceRole::Melody && !lock.pitch && !lock.rhythm &&
               context.patternEnd >= 2 * kTicksPerBar && swapCallAndResponse(notes, context, rng);
    }
    return false;
}

namespace {

constexpr std::array<StructuralOperator, 6> kStructuralOperators = {
    StructuralOperator::ShiftRhythm, StructuralOperator::SwapTones,      StructuralOperator::OctaveJump,
    StructuralOperator::Density,     StructuralOperator::InvertOrMirror, StructuralOperator::CallResponse};

size_t countChanged(const std::vector<Note>& before, const std::vector<Note>& after) {
    std::map<uint32_t, const Note*> byId;
    for (const Note& note : before) {
        byId[note.id] = &note;
    }
    size_t changed = 0;
    for (const Note& note : after) {
        const auto it = byId.find(note.id);
        if (it == byId.end() || !(*it->second == note)) {
            ++changed;
        }
    }
    for (const Note& note : before) { // removed notes count as changed
        changed += std::none_of(after.begin(), after.end(), [&](const Note& n) { return n.id == note.id; }) ? 1 : 0;
    }
    return changed;
}

} // namespace

size_t structuralEditCount(int strengthPct) {
    if (strengthPct < kStructuralFromPct) {
        return 0;
    }
    return strengthPct < 60 ? 1 : (strengthPct < 90 ? 2 : 3);
}

size_t variationEditCount(size_t noteCount, int strengthPct) {
    if (noteCount == 0 || strengthPct <= 0) {
        return 0;
    }
    const auto strength = static_cast<size_t>(std::min(strengthPct, 100));
    const size_t scaled = (noteCount * kSubtleMaxSharePct * strength + 5000) / 10000;
    return std::max<size_t>(1, scaled);
}

size_t applyVariation(Pattern& pattern, const StyleProfile& style, const ArchetypeSettings& settings,
                      const VariationRequest& request) {
    if (request.strengthPct <= 0 || (request.voice && *request.voice >= pattern.voices.size())) {
        return 0;
    }
    const Scale* scale = findScale(pattern.context.scaleId);
    if (scale == nullptr) {
        return 0;
    }
    Pattern next = pattern;
    const ConstraintSettings constraints = constraintSettingsFor(next, style);
    size_t changed = 0;
    for (size_t v = 0; v < next.voices.size(); ++v) {
        Track& track = next.voices[v];
        if ((request.voice && *request.voice != v) || isVoiceLocked(track) || track.notes.empty()) {
            continue;
        }
        VariationVoice context;
        context.scale = scale;
        context.harmony = &next.context;
        context.patternEnd = next.lengthBars * kTicksPerBar;
        context.role = track.role;
        context.phrases = &next.phrases;
        if (v < constraints.voices.size()) {
            context.rangeLow = constraints.voices[v].rangeLow;
            context.rangeHigh = constraints.voices[v].rangeHigh;
        }
        Pcg32 rng = Pcg32::fromSeed(deriveSeed(request.seed, v));
        const std::vector<Note> before = track.notes;
        context.original = &before;
        const size_t edits = variationEditCount(track.notes.size(), request.strengthPct);
        size_t done = 0;
        // Operators run until `edits` notes differ. One may find nothing to do (no accent, no room, no allowed
        // neighbour) or undo an earlier edit: draw again, bounded.
        for (size_t attempt = 0; done < edits && attempt < edits * 8 + 16; ++attempt) {
            const auto op = kSubtleOperators[rng.bounded(static_cast<uint32_t>(kSubtleOperators.size()))];
            if (applyOperator(op, track.notes, track.lock, request.strengthPct, context, rng)) {
                done = countChanged(before, track.notes);
            }
        }
        // From `kStructuralFromPct` on, structural operators change the rhythm, the pitches or the density as well.
        const size_t structural = structuralEditCount(request.strengthPct);
        size_t applied = 0;
        for (size_t attempt = 0; applied < structural && attempt < 12; ++attempt) {
            const auto op = kStructuralOperators[rng.bounded(static_cast<uint32_t>(kStructuralOperators.size()))];
            if (applyStructural(op, track.notes, next, track.lock, context, rng)) {
                ++applied;
            }
        }
        changed += applied > 0 ? countChanged(before, track.notes) : done;
    }
    if (changed == 0) {
        return 0;
    }
    applyConstraints(next, constraintSettingsFor(next, style));
    const QualityContext quality = qualityContextFor(next, style, settings);
    next.qualityScore = static_cast<uint8_t>(
        std::clamp(overallScore(scoreCriteria(next, quality), style.quality, settings.creativityPct), 0, 100));
    next.info.source = "variation";
    pattern = std::move(next);
    return changed;
}

} // namespace mm::core
