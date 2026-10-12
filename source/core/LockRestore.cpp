#include "core/LockRestore.h"

#include <algorithm>
#include <cstdlib>

namespace mm::core {

namespace {

bool anyFlag(const LockFlags& lock) {
    return lock.pitch || lock.rhythm || lock.velocity;
}

bool noteLess(const Note& a, const Note& b) {
    if (a.startTick != b.startTick) {
        return a.startTick < b.startTick;
    }
    return a.pitch < b.pitch;
}

const Note& turn(const std::vector<Note>& notes, size_t index) {
    return notes[index % notes.size()];
}

void takeVelocity(Note& target, const Note& source) {
    target.velocity = source.velocity;
    target.accent = source.accent;
}

} // namespace

bool hasAnyLock(const Track& track) {
    return anyFlag(track.lock) ||
           std::any_of(track.notes.begin(), track.notes.end(), [](const Note& n) { return anyFlag(n.lock); });
}

bool hasAnyLock(const Pattern& pattern) {
    return std::any_of(pattern.voices.begin(), pattern.voices.end(), [](const Track& t) { return hasAnyLock(t); });
}

std::vector<Note> restoreLocks(const std::vector<Note>& old, const std::vector<Note>& fresh, const LockFlags& voiceLock) {
    if (old.empty()) {
        return fresh;
    }
    std::vector<Note> result;
    if (voiceLock.rhythm) {
        for (size_t i = 0; i < old.size(); ++i) {
            Note note = old[i];
            if (!fresh.empty()) {
                if (!(voiceLock.pitch || old[i].lock.pitch)) {
                    note.pitch = turn(fresh, i).pitch;
                }
                if (!(voiceLock.velocity || old[i].lock.velocity)) {
                    takeVelocity(note, turn(fresh, i));
                }
            }
            result.push_back(note);
        }
        std::stable_sort(result.begin(), result.end(), noteLess);
        return result;
    }

    result = fresh;
    for (size_t i = 0; i < result.size(); ++i) {
        if (voiceLock.pitch) {
            result[i].pitch = turn(old, i).pitch;
        }
        if (voiceLock.velocity) {
            takeVelocity(result[i], turn(old, i));
        }
    }
    std::vector<bool> used(result.size(), false);
    std::vector<Note> restored;
    // notes locked in their rhythm stay where they are
    for (const Note& note : old) {
        if (!note.lock.rhythm) {
            continue;
        }
        Note kept = note;
        for (size_t i = 0; i < result.size();) {
            if (result[i].startTick == note.startTick) {
                if (!(voiceLock.pitch || note.lock.pitch)) {
                    kept.pitch = result[i].pitch;
                }
                if (!(voiceLock.velocity || note.lock.velocity)) {
                    takeVelocity(kept, result[i]);
                }
                result.erase(result.begin() + static_cast<std::ptrdiff_t>(i));
                used.erase(used.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        restored.push_back(kept);
    }
    // notes locked in pitch or velocity only: the nearest free fresh note takes the value
    for (const Note& note : old) {
        if (note.lock.rhythm || !(note.lock.pitch || note.lock.velocity)) {
            continue;
        }
        size_t best = result.size();
        for (size_t i = 0; i < result.size(); ++i) {
            if (used[i]) {
                continue;
            }
            const auto distance = [&](size_t index) {
                return std::abs(static_cast<int64_t>(result[index].startTick) - static_cast<int64_t>(note.startTick));
            };
            if (best == result.size() || distance(i) < distance(best)) {
                best = i;
            }
        }
        if (best == result.size()) {
            continue;
        }
        used[best] = true;
        if (note.lock.pitch) {
            result[best].pitch = note.pitch;
            result[best].lock.pitch = true;
        }
        if (note.lock.velocity) {
            takeVelocity(result[best], note);
            result[best].lock.velocity = true;
        }
    }
    result.insert(result.end(), restored.begin(), restored.end());
    std::stable_sort(result.begin(), result.end(), noteLess);
    return result;
}

} // namespace mm::core
