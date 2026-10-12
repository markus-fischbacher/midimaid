#pragma once

#include "core/Pattern.h"

#include <string_view>

namespace mm::core {

/// How the rhythm of the melody relates to the hi-hats of a drum reference (SPEC 3.14, D-228).
enum class HatMode {
    Free,    ///< no relation (default)
    With,    ///< notes prefer the positions of the hats
    Against, ///< notes fill the gaps between the hats
};

std::string_view toString(HatMode mode);
/// Unknown text gives `Free`, so that states of later versions still load.
HatMode parseHatMode(std::string_view text);

/// Moves the melody notes of `pattern` by one 16th towards the hat steps of `pattern.rhythmRef` (`With`) or away from
/// them (`Against`). Only notes on the 16th grid that are not on a beat move (the beats anchor the line), chords move
/// together, a note moves only to a free step inside the pattern, to the nearer side first (the earlier one on a tie).
/// Voices and notes with a locked rhythm, slides and voices that are not melodies stay. Without a reference, without
/// hats or in `Free` mode nothing changes. The constraint layer runs afterwards (overlaps, clipping).
void applyHatMode(Pattern& pattern, HatMode mode);

} // namespace mm::core
