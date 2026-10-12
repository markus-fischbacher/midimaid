#pragma once

#include "core/Pattern.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mm::core {

/// Note edits of the piano roll (SPEC 3.5) as pure functions on a pattern. Each one works on one voice
/// (`voice` is an index into `Pattern::voices`), takes note ids, ignores ids it does not find and returns the number of
/// notes it actually changed: 0 means the pattern is untouched. A change keeps the pattern valid: notes stay inside the
/// pattern, sorted by start tick (stable), ids never change. An edit is literal: overlaps are not cut, locks do not
/// apply (they only protect against generate and variation, SPEC 3.5). A changed pattern gets `info.source` "edit".

/// Adds a note and returns its new id (0 when nothing was added: unknown voice, pitch above 127, velocity outside
/// 1-127, empty length or a note that does not fit into the pattern).
uint32_t addNote(Pattern& pattern, size_t voice, uint8_t pitch, uint32_t startTick, uint32_t lengthTicks,
                 uint8_t velocity);

/// Locks or unlocks the whole voice (all three dimensions). The locks of single notes stay as they are. Returns 1 when
/// the lock changed and 0 for an unknown voice or a voice that is already in that state. `info.source` stays: locking
/// does not edit notes.
size_t setVoiceLocked(Pattern& pattern, size_t voice, bool locked);

/// Removes the notes.
size_t removeNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids);

/// Moves the notes as one block by `deltaTicks` and `deltaPitch`. The block stays rigid: the move is cut back so that
/// no note leaves the pattern or the range 0-127.
size_t moveNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, int32_t deltaTicks, int deltaPitch);

/// Sets the length of every note (cut to the end of the pattern, at least 1 tick).
size_t setLength(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, uint32_t lengthTicks);
/// Adds `deltaTicks` to the length of every note (each cut to 1 tick and the end of the pattern).
size_t resizeNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, int32_t deltaTicks);

struct VelocityChange {
    uint32_t id = 0;
    uint8_t velocity = 100; ///< 1-127; other values are cut into that range
};
/// Sets the base velocity per note (the velocity lane edits several bars at once).
size_t setVelocities(Pattern& pattern, size_t voice, std::span<const VelocityChange> changes);

/// Copies the notes right behind themselves: the offset is the span from the first start to the last end of the
/// selection, rounded up to a multiple of `gridTicks` (0 counts as 1). Copies get new ids and keep pitch, length, velocity,
/// accent, slide and note locks; copies that no longer fit into the pattern are dropped. Returns the number of copies
/// added; their ids go to `newIds` when given (cleared first).
size_t duplicateNotes(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, uint32_t gridTicks,
                      std::vector<uint32_t>* newIds = nullptr);

/// Slide and accent flags. A slide removes a ratchet (the two never combine).
size_t setSlide(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, bool slide);
size_t setAccent(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, bool accent);

/// Ratchet of the notes: `repeats` 1 (off) to 4, other values are limited to that range. A ratchet removes the slide of
/// the note (the two never combine). Returns the number of notes that changed.
size_t setRatchet(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, int repeats);

/// The dimensions a lock can cover (SPEC 3.5, D-231).
enum class LockDimension { Pitch, Rhythm, Velocity };

/// Sets the lock of one dimension for the whole voice (`setVoiceLocked` sets all three). Returns 1 if it changed.
size_t setVoiceLock(Pattern& pattern, size_t voice, LockDimension dimension, bool locked);

/// Sets the lock of one dimension on the notes. Returns the number of notes that changed.
size_t setNoteLock(Pattern& pattern, size_t voice, std::span<const uint32_t> ids, LockDimension dimension, bool locked);

} // namespace mm::core
