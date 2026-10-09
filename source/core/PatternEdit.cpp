#include "core/PatternEdit.h"

#include <algorithm>
#include <limits>

namespace mm::core {

namespace {

uint32_t patternEnd(const Pattern& pattern) {
    return pattern.lengthBars * kTicksPerBar;
}

bool contains(std::span<const uint32_t> ids, uint32_t id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void sortNotes(Track& track) {
    std::stable_sort(track.notes.begin(), track.notes.end(),
                     [](const Note& a, const Note& b) { return a.startTick < b.startTick; });
}

/// Runs `change` on every note of the voice with one of the ids; `change` returns true when it changed the note.
template <typename Change>
size_t forNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, Change change) {
    if (voice >= pattern.voices.size()) {
        return 0;
    }
    size_t changed = 0;
    for (auto& note : pattern.voices[voice].notes) {
        if (contains(ids, note.id) && change(note)) {
            ++changed;
        }
    }
    if (changed > 0) {
        pattern.info.source = "edit";
    }
    return changed;
}

} // namespace

uint32_t addNote(Pattern& pattern, size_t voice, uint8_t pitch, uint32_t startTick, uint32_t lengthTicks,
                 uint8_t velocity) {
    if (voice >= pattern.voices.size() || pitch > 127 || velocity < 1 || velocity > 127 || lengthTicks < 1 ||
        static_cast<uint64_t>(startTick) + lengthTicks > patternEnd(pattern)) {
        return 0;
    }
    Note note;
    note.id = allocateNoteId(pattern);
    note.pitch = pitch;
    note.startTick = startTick;
    note.lengthTicks = lengthTicks;
    note.velocity = velocity;
    auto& track = pattern.voices[voice];
    // After the notes that start at the same tick: the new note is the last of its position.
    const auto at = std::upper_bound(track.notes.begin(), track.notes.end(), startTick,
                                     [](uint32_t tick, const Note& other) { return tick < other.startTick; });
    track.notes.insert(at, note);
    pattern.info.source = "edit";
    return note.id;
}

size_t setVoiceLocked(Pattern& pattern, size_t voice, bool locked) {
    if (voice >= pattern.voices.size()) {
        return 0;
    }
    auto& lock = pattern.voices[voice].lock;
    if (lock.pitch == locked && lock.rhythm == locked && lock.velocity == locked) {
        return 0;
    }
    lock.pitch = lock.rhythm = lock.velocity = locked;
    return 1;
}

size_t removeNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids) {
    if (voice >= pattern.voices.size()) {
        return 0;
    }
    auto& notes = pattern.voices[voice].notes;
    const auto removed = std::remove_if(notes.begin(), notes.end(), [&](const Note& n) { return contains(ids, n.id); });
    const auto count = static_cast<size_t>(notes.end() - removed);
    notes.erase(removed, notes.end());
    if (count > 0) {
        pattern.info.source = "edit";
    }
    return count;
}

size_t moveNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, int32_t deltaTicks, int deltaPitch) {
    if (voice >= pattern.voices.size()) {
        return 0;
    }
    int64_t minTicks = std::numeric_limits<int64_t>::min();
    int64_t maxTicks = std::numeric_limits<int64_t>::max();
    int minPitch = std::numeric_limits<int>::min();
    int maxPitch = std::numeric_limits<int>::max();
    size_t found = 0;
    for (const auto& note : pattern.voices[voice].notes) {
        if (!contains(ids, note.id)) {
            continue;
        }
        ++found;
        minTicks = std::max<int64_t>(minTicks, -static_cast<int64_t>(note.startTick));
        maxTicks = std::min<int64_t>(maxTicks, static_cast<int64_t>(patternEnd(pattern)) - note.startTick -
                                                   static_cast<int64_t>(note.lengthTicks));
        minPitch = std::max(minPitch, -static_cast<int>(note.pitch));
        maxPitch = std::min(maxPitch, 127 - static_cast<int>(note.pitch));
    }
    if (found == 0) {
        return 0;
    }
    const auto ticks = static_cast<int32_t>(std::clamp<int64_t>(deltaTicks, minTicks, maxTicks));
    const int pitch = std::clamp(deltaPitch, minPitch, maxPitch);
    const auto changed = forNotes(pattern, voice, ids, [&](Note& note) {
        if (ticks == 0 && pitch == 0) {
            return false;
        }
        note.startTick = static_cast<uint32_t>(static_cast<int64_t>(note.startTick) + ticks);
        note.pitch = static_cast<uint8_t>(note.pitch + pitch);
        return true;
    });
    if (changed > 0 && ticks != 0) {
        sortNotes(pattern.voices[voice]);
    }
    return changed;
}

size_t setLength(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, uint32_t lengthTicks) {
    const uint32_t end = patternEnd(pattern);
    return forNotes(pattern, voice, ids, [&](Note& note) {
        const uint32_t room = end - note.startTick;
        const uint32_t length = std::clamp<uint32_t>(lengthTicks, 1, room);
        const bool differs = note.lengthTicks != length;
        note.lengthTicks = length;
        return differs;
    });
}

size_t resizeNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, int32_t deltaTicks) {
    const uint32_t end = patternEnd(pattern);
    return forNotes(pattern, voice, ids, [&](Note& note) {
        const int64_t wanted = static_cast<int64_t>(note.lengthTicks) + deltaTicks;
        const auto length = static_cast<uint32_t>(std::clamp<int64_t>(wanted, 1, end - note.startTick));
        const bool differs = note.lengthTicks != length;
        note.lengthTicks = length;
        return differs;
    });
}

size_t setVelocities(Pattern& pattern, size_t voice, std::span<const VelocityChange> changes) {
    if (voice >= pattern.voices.size()) {
        return 0;
    }
    size_t changed = 0;
    for (auto& note : pattern.voices[voice].notes) {
        for (const auto& change : changes) {
            if (change.id == note.id) {
                const auto velocity = static_cast<uint8_t>(std::clamp<int>(change.velocity, 1, 127));
                if (note.velocity != velocity) {
                    note.velocity = velocity;
                    ++changed;
                }
                break;
            }
        }
    }
    if (changed > 0) {
        pattern.info.source = "edit";
    }
    return changed;
}

size_t setSlide(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, bool slide) {
    return forNotes(pattern, voice, ids, [&](Note& note) {
        const bool differs = note.slide != slide || (slide && note.ratchet > 1);
        note.slide = slide;
        if (slide) {
            note.ratchet = 1;
        }
        return differs;
    });
}

size_t setAccent(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, bool accent) {
    return forNotes(pattern, voice, ids, [&](Note& note) {
        const bool differs = note.accent != accent;
        note.accent = accent;
        return differs;
    });
}

size_t duplicateNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, uint32_t gridTicks,
                      std::vector<uint32_t>* newIds) {
    if (newIds != nullptr) {
        newIds->clear();
    }
    if (voice >= pattern.voices.size()) {
        return 0;
    }
    auto& track = pattern.voices[voice];
    std::vector<Note> originals;
    for (const auto& note : track.notes) {
        if (contains(ids, note.id)) {
            originals.push_back(note);
        }
    }
    if (originals.empty()) {
        return 0;
    }
    uint64_t first = std::numeric_limits<uint64_t>::max();
    uint64_t last = 0;
    for (const auto& note : originals) {
        first = std::min<uint64_t>(first, note.startTick);
        last = std::max<uint64_t>(last, static_cast<uint64_t>(note.startTick) + note.lengthTicks);
    }
    const uint64_t grid = std::max<uint32_t>(gridTicks, 1);
    const uint64_t span = last - first;
    const uint64_t shift = (span + grid - 1) / grid * grid;
    size_t added = 0;
    for (auto copy : originals) {
        if (static_cast<uint64_t>(copy.startTick) + shift + copy.lengthTicks > patternEnd(pattern)) {
            continue;
        }
        copy.id = allocateNoteId(pattern);
        copy.startTick = static_cast<uint32_t>(copy.startTick + shift);
        track.notes.push_back(copy);
        if (newIds != nullptr) {
            newIds->push_back(copy.id);
        }
        ++added;
    }
    if (added > 0) {
        sortNotes(track);
        pattern.info.source = "edit";
    }
    return added;
}

} // namespace mm::core
