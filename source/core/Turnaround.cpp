#include "core/Turnaround.h"

#include <algorithm>
#include <array>
#include <set>

namespace mm::core {

namespace {

constexpr uint32_t kBeat = kTicksPerBar / 4;
constexpr uint32_t kSixteenth = kBeat / 4;
constexpr uint64_t kTurnaroundStream = 0x7475726eULL; // "turn"

bool isChordal(const std::vector<Note>& notes, uint32_t from, uint32_t to) {
    std::set<uint32_t> starts;
    for (const Note& note : notes) {
        if (note.startTick >= from && note.startTick < to && !starts.insert(note.startTick).second) {
            return true;
        }
    }
    return false;
}

void pause(Track& track, uint32_t barStart) {
    const uint32_t cut = barStart + 2 * kBeat;
    auto& notes = track.notes;
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const Note& n) { return n.startTick >= cut && n.startTick < barStart + kTicksPerBar; }),
                notes.end());
    // a note that reaches into the silence ends at its start
    for (Note& note : notes) {
        if (note.startTick >= barStart && note.startTick < cut && note.startTick + note.lengthTicks > cut) {
            note.lengthTicks = cut - note.startTick;
        }
    }
}

void fill(Pattern& pattern, size_t voice, uint32_t barStart, Pcg32& rng) {
    Track& track = pattern.voices[voice];
    const uint32_t beatStart = barStart + 3 * kBeat;
    // the pitches of the bar give the run its material (sorted, so that the run is a line)
    std::set<int> pitchSet;
    for (const Note& note : track.notes) {
        if (note.startTick >= barStart && note.startTick < barStart + kTicksPerBar) {
            pitchSet.insert(note.pitch);
        }
    }
    if (pitchSet.empty()) {
        return;
    }
    std::vector<int> pitches(pitchSet.begin(), pitchSet.end());
    const bool descending = rng.chance(30);
    if (descending) {
        std::reverse(pitches.begin(), pitches.end());
    }
    int baseVelocity = 90;
    for (const Note& note : track.notes) {
        if (note.startTick >= barStart && note.startTick < beatStart) {
            baseVelocity = note.velocity;
        }
    }
    auto& notes = track.notes;
    // a note that reaches into the last beat ends where the run starts
    for (Note& note : notes) {
        if (note.startTick >= barStart && note.startTick < beatStart && note.startTick + note.lengthTicks > beatStart) {
            note.lengthTicks = beatStart - note.startTick;
        }
    }
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&](const Note& n) { return n.startTick >= beatStart && n.startTick < barStart + kTicksPerBar; }),
                notes.end());
    for (uint32_t step = 0; step < 4; ++step) {
        Note note;
        note.id = allocateNoteId(pattern);
        note.pitch = static_cast<uint8_t>(pitches[step % pitches.size()]);
        note.startTick = beatStart + step * kSixteenth;
        note.lengthTicks = kSixteenth * 3 / 4;
        note.velocity = static_cast<uint8_t>(std::clamp<int>(baseVelocity - 10 + static_cast<int>(step) * 8, 1, 127));
        note.accent = step == 3;
        notes.push_back(note);
    }
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
}

void octaveJump(Track& track, uint32_t barStart) {
    int low = 127;
    int high = 0;
    for (const Note& note : track.notes) {
        low = std::min<int>(low, note.pitch);
        high = std::max<int>(high, note.pitch);
    }
    for (Note& note : track.notes) {
        if (note.startTick < barStart + 2 * kBeat || note.startTick >= barStart + kTicksPerBar) {
            continue;
        }
        // up if the voice has room for it, else down; the constraint layer folds what is left
        if (note.pitch + 12 <= high + 5) {
            note.pitch = static_cast<uint8_t>(note.pitch + 12);
        } else if (note.pitch - 12 >= low - 5) {
            note.pitch = static_cast<uint8_t>(note.pitch - 12);
        }
    }
}

/// The last note that starts in the second half of the bar (else the last note of the bar) gets 2 to 4 repeats; its
/// length grows to a 16th when the bar has room for it. Other ratchets of the bar go (one per bar), and so does a slide
/// into the note. With chords every note of the start repeats.
void ratchet(Track& track, uint32_t barStart, Pcg32& rng) {
    const uint32_t barEnd = barStart + kTicksPerBar;
    auto& notes = track.notes;
    int64_t target = -1;
    uint32_t targetStart = 0;
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].startTick >= barStart && notes[i].startTick < barEnd) {
            const bool late = notes[i].startTick >= barStart + 2 * kBeat;
            const bool targetLate = target >= 0 && targetStart >= barStart + 2 * kBeat;
            if (target < 0 || late || !targetLate) {
                target = static_cast<int64_t>(i);
                targetStart = notes[i].startTick;
            }
            notes[i].ratchet = 1;
        }
    }
    if (target < 0) {
        return;
    }
    const uint32_t repeatsWanted = 2 + rng.bounded(3);
    for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].startTick != targetStart) {
            continue;
        }
        Note& note = notes[i];
        if (note.startTick + kSixteenth <= barEnd && note.lengthTicks < kSixteenth) {
            note.lengthTicks = kSixteenth;
        }
        note.slide = false;
        const uint32_t most = static_cast<uint32_t>(std::clamp<uint32_t>(note.lengthTicks / 60, 1, 4));
        note.ratchet = static_cast<uint8_t>(std::min(repeatsWanted, most));
        if (i > 0) {
            notes[i - 1].slide = notes[i - 1].slide && notes[i - 1].startTick == note.startTick;
        }
    }
}

} // namespace

int turnaroundChancePct(int energyPct) {
    return std::clamp(10 + std::clamp(energyPct, 0, 100) * 6 / 10, 0, 100);
}

void assignTurnarounds(std::vector<Phrase>& phrases, int energyPct, uint64_t seed) {
    Pcg32 rng = Pcg32::fromSeed(deriveSeed(seed, kTurnaroundStream));
    for (Phrase& phrase : phrases) {
        if (phrase.lengthBars < 8) {
            continue;
        }
        const bool drawn = rng.chance(turnaroundChancePct(energyPct));
        phrase.turnaround = phrase.turnaround || drawn;
    }
}

TurnaroundKind chooseTurnaroundKind(bool chordal, int energyPct, Pcg32& rng) {
    const int energy = std::clamp(energyPct, 0, 100);
    const std::array<uint32_t, 4> weights{
        static_cast<uint32_t>(60 - energy / 2),         // pause
        chordal ? 0u : static_cast<uint32_t>(20 + energy / 2), // fill
        static_cast<uint32_t>(20 + energy / 5),         // octave jump
        static_cast<uint32_t>(10 + energy / 4),         // ratchet
    };
    const size_t index = rng.weightedIndex(weights);
    return index == 1   ? TurnaroundKind::Fill
           : index == 2 ? TurnaroundKind::OctaveJump
           : index == 3 ? TurnaroundKind::Ratchet
                        : TurnaroundKind::Pause;
}

void applyTurnaround(Pattern& pattern, const Phrase& phrase, int energyPct, uint64_t seed,
                     const std::vector<bool>& varies) {
    if (!phrase.turnaround || phrase.lengthBars < 8) {
        return;
    }
    const uint32_t barStart = (phrase.startBar + phrase.lengthBars - 1) * kTicksPerBar;
    Pcg32 rng = Pcg32::fromSeed(deriveSeed(seed, kTurnaroundStream));
    for (size_t i = 0; i < pattern.voices.size(); ++i) {
        // every voice draws, so that skipping one never shifts the draw of the next
        const bool chordal = isChordal(pattern.voices[i].notes, barStart, barStart + kTicksPerBar);
        const TurnaroundKind kind = chooseTurnaroundKind(chordal, energyPct, rng);
        const bool wanted = varies.empty() || (i < varies.size() && varies[i]);
        const LockFlags& lock = pattern.voices[i].lock;
        if (!wanted || lock.rhythm || lock.pitch) {
            continue;
        }
        switch (kind) {
        case TurnaroundKind::Pause:
            pause(pattern.voices[i], barStart);
            break;
        case TurnaroundKind::Fill:
            fill(pattern, i, barStart, rng);
            break;
        case TurnaroundKind::OctaveJump:
            octaveJump(pattern.voices[i], barStart);
            break;
        case TurnaroundKind::Ratchet:
            ratchet(pattern.voices[i], barStart, rng);
            break;
        }
    }
}

} // namespace mm::core
