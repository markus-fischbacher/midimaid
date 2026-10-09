#include "core/Variation.h"

#include "core/Random.h"
#include "core/Theory.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <map>

namespace mm::core {

namespace {

constexpr uint32_t kLengthStepTicks = 60; // 1/64
constexpr uint32_t kHalfBarTicks = kTicksPerBar / 2;

enum class SubtleOperator { Length, VelocityContour, MoveAccent, ReplaceNote };

struct VoiceContext {
    const Scale* scale = nullptr;
    const HarmonicContext* harmony = nullptr;
    int rangeLow = 0;
    int rangeHigh = 127;
    uint32_t patternEnd = 0;
    const std::vector<Note>* original = nullptr; ///< the voice before this variation (same indices while D1 only edits)
};

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
uint32_t roomAfter(const std::vector<Note>& notes, size_t index, const VoiceContext& context) {
    const uint32_t start = notes[index].startTick;
    uint32_t limit = context.patternEnd;
    if (index + 1 < notes.size()) {
        limit = std::min(limit, notes[index + 1].startTick);
    }
    return limit > start ? limit - start : 0;
}

bool changeLength(std::vector<Note>& notes, int strengthPct, const VoiceContext& context, Pcg32& rng) {
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

bool replaceNote(std::vector<Note>& notes, const VoiceContext& context, Pcg32& rng) {
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
                   const VoiceContext& context, Pcg32& rng) {
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
    return changed;
}

} // namespace

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
        VoiceContext context;
        context.scale = scale;
        context.harmony = &next.context;
        context.patternEnd = next.lengthBars * kTicksPerBar;
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
        changed += done;
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
