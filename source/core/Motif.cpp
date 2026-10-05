#include "core/Motif.h"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace mm::core {

namespace {

constexpr uint32_t kMinLengthTicks = 60;
constexpr int kMaxVelocityOffset = 20;
constexpr int kAttempts = 400;

int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0))) {
        --quotient;
    }
    return quotient;
}

int pitchOf(const Motif& motif, size_t index, const MotifRules& rules) {
    return scaleStepToMidi(*rules.scale, rules.basePitch, motif.notes[index].degree);
}

bool chordToneAtStep(int step, const MotifRules& rules) {
    return isChordTone(rules.keyRoot, rules.referenceChord, scaleStepToMidi(*rules.scale, rules.basePitch, step));
}

bool isStrongStep(uint8_t step) {
    return step % 4 == 0;
}

uint32_t motifEndTicks(const Motif& motif) {
    return static_cast<uint32_t>(motif.bars) * kStepsPerBar * kTicksPerStep;
}

/// Ticks from the start of note `index` to the start of the next note or the end of the motif.
uint32_t gapTicks(const Motif& motif, size_t index) {
    const uint32_t start = motif.notes[index].startStep * kTicksPerStep;
    const uint32_t next =
        index + 1 < motif.notes.size() ? motif.notes[index + 1].startStep * kTicksPerStep : motifEndTicks(motif);
    return next - start;
}

/// The chord-tone step nearest to `step` (ties go down), searched up to 7 steps away; nullopt if none.
std::optional<int> nearestChordToneStep(int step, const MotifRules& rules) {
    for (int distance = 0; distance <= 7; ++distance) {
        if (chordToneAtStep(step - distance, rules)) {
            return step - distance;
        }
        if (chordToneAtStep(step + distance, rules)) {
            return step + distance;
        }
    }
    return std::nullopt;
}

/// Moves notes on strong steps that are not chord tones to the nearest chord tone. False if one cannot be repaired.
bool repairStrongSteps(Motif& motif, const MotifRules& rules) {
    for (MotifNote& note : motif.notes) {
        if (isStrongStep(note.startStep) && !chordToneAtStep(note.degree, rules)) {
            const auto repaired = nearestChordToneStep(note.degree, rules);
            if (!repaired || *repaired < std::numeric_limits<int8_t>::min() ||
                *repaired > std::numeric_limits<int8_t>::max()) {
                return false;
            }
            note.degree = static_cast<int8_t>(*repaired);
        }
    }
    return true;
}

bool inRangeAndSpan(const Motif& motif, const MotifRules& rules) {
    int low = 128;
    int high = -1;
    for (size_t i = 0; i < motif.notes.size(); ++i) {
        const int pitch = pitchOf(motif, i, rules);
        if (!rules.range.contains(pitch)) {
            return false;
        }
        low = std::min(low, pitch);
        high = std::max(high, pitch);
    }
    return high - low <= rules.maxSpanSemitones;
}

/// Weighted pick of an index (0 if all weights are zero).
size_t pick(Pcg32& rng, std::vector<uint32_t> weights) {
    const size_t index = rng.weightedIndex(weights);
    return index < weights.size() ? index : 0;
}

} // namespace

int scaleStepToMidi(const Scale& scale, int basePitch, int step) {
    const int size = static_cast<int>(scale.size());
    const int octave = floorDiv(step, size);
    const int within = step - octave * size;
    return basePitch + 12 * octave + scale.intervals[static_cast<size_t>(within)];
}

size_t weakStepCount(const Motif& motif) {
    return static_cast<size_t>(std::count_if(motif.notes.begin(), motif.notes.end(),
                                             [](const MotifNote& n) { return !isStrongStep(n.startStep); }));
}

size_t weakStepNonChordCount(const Motif& motif, const MotifRules& rules) {
    size_t count = 0;
    for (const MotifNote& note : motif.notes) {
        if (!isStrongStep(note.startStep) && !chordToneAtStep(note.degree, rules)) {
            ++count;
        }
    }
    return count;
}

bool isValidMotif(const Motif& motif, const MotifRules& rules) {
    if (rules.scale == nullptr || rules.scale->size() == 0 || motif.bars < 1 || motif.bars > 2 || motif.notes.empty()) {
        return false;
    }
    const int steps = motif.bars * kStepsPerBar;
    for (size_t i = 0; i < motif.notes.size(); ++i) {
        const MotifNote& note = motif.notes[i];
        if (note.startStep >= steps || note.lengthTicks < kMinLengthTicks || note.lengthTicks % kMinLengthTicks != 0 ||
            note.velocityOffset < -kMaxVelocityOffset || note.velocityOffset > kMaxVelocityOffset) {
            return false;
        }
        if (i > 0 && note.startStep <= motif.notes[i - 1].startStep) {
            return false; // sorted, one note per step
        }
        if (note.lengthTicks > gapTicks(motif, i)) {
            return false; // overlap or beyond the end
        }
        if (isStrongStep(note.startStep) && !chordToneAtStep(note.degree, rules)) {
            return false;
        }
    }
    if (!inRangeAndSpan(motif, rules)) {
        return false;
    }
    // weak steps: a share of at least 20 % must be non-chord tones, and every one of them is a passing or neighbour
    // tone
    const size_t weak = weakStepCount(motif);
    const size_t nonChord = weakStepNonChordCount(motif, rules);
    if (nonChord * 5 < weak) {
        return false;
    }
    for (size_t i = 0; i < motif.notes.size(); ++i) {
        const MotifNote& note = motif.notes[i];
        if (isStrongStep(note.startStep) || chordToneAtStep(note.degree, rules)) {
            continue;
        }
        if (i == 0 || i + 1 == motif.notes.size()) {
            return false;
        }
        if (std::abs(note.degree - motif.notes[i - 1].degree) != 1 ||
            std::abs(motif.notes[i + 1].degree - note.degree) != 1) {
            return false;
        }
    }
    return true;
}

std::optional<Motif> generateMotif(const MotifParams& params, const MotifRules& rules, Pcg32& rng) {
    if (rules.scale == nullptr || rules.scale->size() == 0 || params.bars < 1 || params.bars > 2 ||
        params.minNotes < 1 || params.maxNotes < params.minNotes) {
        return std::nullopt;
    }
    const int steps = params.bars * kStepsPerBar;

    // the chord-tone steps whose pitch lies in the range
    std::vector<int> chordSteps;
    for (int step = -40; step <= 40; ++step) {
        if (chordToneAtStep(step, rules) &&
            rules.range.contains(scaleStepToMidi(*rules.scale, rules.basePitch, step))) {
            chordSteps.push_back(step);
        }
    }
    if (chordSteps.empty()) {
        return std::nullopt;
    }

    for (int attempt = 0; attempt < kAttempts; ++attempt) {
        const int noteCount = std::min(rng.range(params.minNotes, params.maxNotes), steps);
        int pickups = 0;
        if (params.allowPickup && noteCount >= 3 && rng.chance(params.pickupPercent)) {
            pickups = rng.range(1, 2);
        }

        // rhythm: offbeats and off-16ths are likely, the downbeat of the motif is likely, other strong steps less so
        std::vector<uint32_t> weights(static_cast<size_t>(steps), 0);
        for (int step = 0; step < steps - pickups; ++step) {
            weights[static_cast<size_t>(step)] = step == 0 ? 3 : step % 4 == 2 ? 4 : step % 2 == 1 ? 3 : 2;
        }
        std::vector<uint8_t> starts;
        for (int i = 0; i < noteCount - pickups; ++i) {
            const size_t step = rng.weightedIndex(weights);
            if (step >= weights.size()) {
                break;
            }
            weights[step] = 0;
            starts.push_back(static_cast<uint8_t>(step));
        }
        for (int i = pickups; i > 0; --i) {
            starts.push_back(static_cast<uint8_t>(steps - i));
        }
        if (static_cast<int>(starts.size()) != noteCount) {
            continue;
        }
        std::sort(starts.begin(), starts.end());

        Motif motif;
        motif.bars = params.bars;
        for (const uint8_t start : starts) {
            MotifNote note;
            note.startStep = start;
            motif.notes.push_back(note);
        }
        for (size_t i = 0; i < motif.notes.size(); ++i) {
            const uint32_t lengthSteps = 1 + static_cast<uint32_t>(pick(rng, {5, 3, 1}));
            const uint32_t unit = rng.chance(40) ? 180 : kTicksPerStep; // staccato notes are 3/4 of a step
            motif.notes[i].lengthTicks = std::min(lengthSteps * unit, gapTicks(motif, i));
        }

        // pitches: a chord-tone skeleton that stays within the span
        int low = 0;
        int high = 0;
        bool failed = false;
        std::vector<int> degrees;
        for (size_t i = 0; i < motif.notes.size() && !failed; ++i) {
            std::vector<int> options;
            std::vector<uint32_t> optionWeights;
            for (const int step : chordSteps) {
                const int pitch = scaleStepToMidi(*rules.scale, rules.basePitch, step);
                if (i > 0) {
                    const int distance = std::abs(step - degrees.back());
                    if (distance > 4) {
                        continue;
                    }
                    if (std::max(high, pitch) - std::min(low, pitch) > rules.maxSpanSemitones) {
                        continue;
                    }
                    options.push_back(step);
                    optionWeights.push_back(distance == 0 ? 2 : distance <= 2 ? 4 : 2);
                } else {
                    options.push_back(step);
                    optionWeights.push_back(1);
                }
            }
            if (options.empty()) {
                failed = true;
                break;
            }
            const int chosen = options[pick(rng, optionWeights)];
            const int pitch = scaleStepToMidi(*rules.scale, rules.basePitch, chosen);
            low = i == 0 ? pitch : std::min(low, pitch);
            high = i == 0 ? pitch : std::max(high, pitch);
            degrees.push_back(chosen);
        }
        if (failed) {
            continue;
        }

        // passing and neighbour tones on weak steps (at least 20 % of the weak notes, never next to each other)
        const size_t weak = static_cast<size_t>(std::count_if(
            motif.notes.begin(), motif.notes.end(), [](const MotifNote& n) { return !isStrongStep(n.startStep); }));
        const size_t needed = (weak + 4) / 5;
        std::vector<size_t> eligible;
        for (size_t i = 1; i + 1 < motif.notes.size(); ++i) {
            if (!isStrongStep(motif.notes[i].startStep)) {
                eligible.push_back(i);
            }
        }
        rng.shuffle(eligible.begin(), eligible.end());
        std::vector<bool> converted(motif.notes.size(), false);
        size_t made = 0;
        for (const size_t i : eligible) {
            if (converted[i - 1] || converted[i + 1]) {
                continue;
            }
            const int between = degrees[i + 1] - degrees[i - 1];
            int candidate = 0;
            if (std::abs(between) == 2) {
                candidate = degrees[i - 1] + between / 2; // passing tone
            } else if (between == 0) {
                candidate = degrees[i - 1] + (rng.chance(50) ? 1 : -1); // neighbour tone
            } else {
                continue;
            }
            const int pitch = scaleStepToMidi(*rules.scale, rules.basePitch, candidate);
            if (chordToneAtStep(candidate, rules) || !rules.range.contains(pitch) ||
                std::max(high, pitch) - std::min(low, pitch) > rules.maxSpanSemitones) {
                continue;
            }
            if (made >= needed && !rng.chance(25)) {
                continue;
            }
            degrees[i] = candidate;
            converted[i] = true;
            low = std::min(low, pitch);
            high = std::max(high, pitch);
            ++made;
        }
        for (size_t i = 0; i < motif.notes.size(); ++i) {
            motif.notes[i].degree = static_cast<int8_t>(degrees[i]);
        }

        if (rng.chance(45) && weak > 0) {
            // one accent on a weak step
            std::vector<size_t> weakNotes;
            for (size_t i = 0; i < motif.notes.size(); ++i) {
                if (!isStrongStep(motif.notes[i].startStep)) {
                    weakNotes.push_back(i);
                }
            }
            motif.notes[weakNotes[rng.bounded(static_cast<uint32_t>(weakNotes.size()))]].accent = true;
        }

        if (isValidMotif(motif, rules)) {
            return motif;
        }
    }
    return std::nullopt;
}

std::vector<bool> planRepetitions(uint32_t blocks, RepetitionStyle style, Pcg32& rng) {
    std::vector<bool> varied(blocks, false);
    size_t position = 0;
    while (position < blocks) {
        const size_t repetitions = style == RepetitionStyle::Hypnotic ? 3 : 2 + pick(rng, {25, 60, 15});
        position += repetitions; // unchanged occurrences
        if (position < blocks) {
            varied[position] = true;
        }
        ++position;
    }
    return varied;
}

std::optional<Motif> varyMotif(const Motif& motif, MotifVariation kind, const MotifRules& rules, Pcg32& rng) {
    if (motif.notes.empty()) {
        return std::nullopt;
    }
    auto accept = [&](const Motif& candidate) { return candidate != motif && isValidMotif(candidate, rules); };

    switch (kind) {
    case MotifVariation::Micro: {
        for (int attempt = 0; attempt < 40; ++attempt) {
            Motif candidate = motif;
            const size_t index = rng.bounded(static_cast<uint32_t>(candidate.notes.size()));
            MotifNote& note = candidate.notes[index];
            switch (rng.bounded(3)) {
            case 0: {
                const int length = static_cast<int>(note.lengthTicks) + (rng.chance(50) ? 60 : -60);
                if (length < static_cast<int>(kMinLengthTicks)) {
                    continue;
                }
                note.lengthTicks = static_cast<uint32_t>(length);
                break;
            }
            case 1:
                note.accent = !note.accent;
                break;
            default: {
                const int offset = note.velocityOffset + (rng.chance(50) ? 8 : -8);
                note.velocityOffset = static_cast<int8_t>(std::clamp(offset, -kMaxVelocityOffset, kMaxVelocityOffset));
                break;
            }
            }
            if (accept(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }
    case MotifVariation::Transpose: {
        std::vector<int> shifts{-2, -1, 1, 2};
        rng.shuffle(shifts.begin(), shifts.end());
        for (const int shift : shifts) {
            Motif candidate = motif;
            for (MotifNote& note : candidate.notes) {
                note.degree = static_cast<int8_t>(note.degree + shift);
            }
            if (repairStrongSteps(candidate, rules) && accept(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }
    case MotifVariation::Invert: {
        Motif candidate = motif;
        const int anchor = motif.notes.front().degree;
        for (MotifNote& note : candidate.notes) {
            note.degree = static_cast<int8_t>(2 * anchor - note.degree);
        }
        if (repairStrongSteps(candidate, rules) && accept(candidate)) {
            return candidate;
        }
        return std::nullopt;
    }
    case MotifVariation::NewEnding: {
        const int current = motif.notes.back().degree;
        std::vector<int> options;
        for (int step = current - 4; step <= current + 4; ++step) {
            if (step != current && chordToneAtStep(step, rules)) {
                options.push_back(step);
            }
        }
        rng.shuffle(options.begin(), options.end());
        for (const int step : options) {
            Motif candidate = motif;
            candidate.notes.back().degree = static_cast<int8_t>(step);
            if (accept(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }
    case MotifVariation::ShiftNote: {
        std::vector<size_t> order(motif.notes.size());
        for (size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        rng.shuffle(order.begin(), order.end());
        for (const size_t index : order) {
            const int direction = rng.chance(50) ? 1 : -1;
            for (const int d : {direction, -direction}) {
                Motif candidate = motif;
                const int step = candidate.notes[index].startStep + d;
                const int steps = candidate.bars * kStepsPerBar;
                const bool afterPrevious = index == 0 || step > candidate.notes[index - 1].startStep;
                const bool beforeNext =
                    index + 1 == candidate.notes.size() || step < candidate.notes[index + 1].startStep;
                if (step < 0 || step >= steps || !afterPrevious || !beforeNext) {
                    continue;
                }
                candidate.notes[index].startStep = static_cast<uint8_t>(step);
                for (size_t i = std::max<size_t>(index, 1) - 1; i <= index; ++i) {
                    candidate.notes[i].lengthTicks = std::min(candidate.notes[i].lengthTicks, gapTicks(candidate, i));
                }
                if (accept(candidate)) {
                    return candidate;
                }
            }
        }
        return std::nullopt;
    }
    }
    return std::nullopt;
}

std::vector<Motif> buildMotifSequence(const Motif& motif, const MotifRules& rules, uint32_t totalBars,
                                      RepetitionStyle style, Pcg32& rng) {
    if (motif.bars == 0 || totalBars < motif.bars || totalBars % motif.bars != 0) {
        return {};
    }
    const std::vector<bool> varied = planRepetitions(totalBars / motif.bars, style, rng);
    std::vector<Motif> sequence;
    for (const bool vary : varied) {
        if (!vary) {
            sequence.push_back(motif);
            continue;
        }
        MotifVariation kind = MotifVariation::Micro;
        if (style == RepetitionStyle::Standard) {
            static const MotifVariation kinds[] = {MotifVariation::Micro, MotifVariation::ShiftNote,
                                                   MotifVariation::NewEnding, MotifVariation::Transpose,
                                                   MotifVariation::Invert};
            kind = kinds[pick(rng, {40, 20, 20, 10, 10})];
        }
        auto result = varyMotif(motif, kind, rules, rng);
        if (!result && kind != MotifVariation::Micro) {
            result = varyMotif(motif, MotifVariation::Micro, rules, rng);
        }
        sequence.push_back(result ? *result : motif);
    }
    return sequence;
}

uint32_t repetitionPermille(const std::vector<Motif>& sequence) {
    if (sequence.empty()) {
        return 0;
    }
    const auto same = static_cast<uint32_t>(std::count(sequence.begin(), sequence.end(), sequence.front()));
    return same * 1000 / static_cast<uint32_t>(sequence.size());
}

std::vector<Note> realizeMotif(const Motif& motif, const MotifRules& rules, int stepShift, uint32_t startTick) {
    std::vector<Note> notes;
    if (rules.scale == nullptr || rules.scale->size() == 0 || rules.range.width() < 12) {
        return notes;
    }
    for (const MotifNote& source : motif.notes) {
        int pitch = scaleStepToMidi(*rules.scale, rules.basePitch, source.degree + stepShift);
        while (pitch < rules.range.low) {
            pitch += 12;
        }
        while (pitch > rules.range.high) {
            pitch -= 12;
        }
        if (!rules.range.contains(pitch)) {
            continue;
        }
        Note note;
        note.pitch = static_cast<uint8_t>(pitch);
        note.startTick = startTick + source.startStep * kTicksPerStep;
        note.lengthTicks = source.lengthTicks;
        note.velocity = static_cast<uint8_t>(std::clamp(100 + source.velocityOffset, 1, 127));
        note.accent = source.accent;
        notes.push_back(note);
    }
    return notes;
}

} // namespace mm::core
