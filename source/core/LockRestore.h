#pragma once

#include "core/Pattern.h"

#include <vector>

namespace mm::core {

/// Locks per dimension (SPEC 3.5, v1.1, D-231): pitch, rhythm (position and length) and velocity (with the accent) can
/// be locked per voice and per note. A voice with all three locked is kept as it is (`isVoiceLocked`); this file is
/// about everything in between.

/// True if the voice has a lock in some dimension or one of its notes has one.
bool hasAnyLock(const Track& track);
/// True if some voice of the pattern does (`hasAnyLock`).
bool hasAnyLock(const Pattern& pattern);

/// What a new generation does with the locks of a voice. `old` are the notes before, `fresh` the notes just generated
/// for the voice (with their own ids); the result takes the locked dimensions from `old` and the rest from `fresh`:
///  - voice lock rhythm: the positions and lengths of `old` stay, pitch and velocity come from `fresh` in turn;
///  - voice lock pitch: the pitch sequence of `old` runs through the new rhythm, voice lock velocity likewise;
///  - note lock rhythm: the note stays where it is (fresh notes that start there give way), a pitch or velocity that is
///    not locked is taken from the fresh note that stood there;
///  - note lock pitch or velocity alone: the nearest fresh note takes the locked value.
/// Notes that were locked keep their note locks. The result is sorted by start and pitch.
std::vector<Note> restoreLocks(const std::vector<Note>& old, const std::vector<Note>& fresh, const LockFlags& voiceLock);

} // namespace mm::core
