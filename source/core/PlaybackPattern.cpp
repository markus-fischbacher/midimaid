#include "core/PlaybackPattern.h"

#include <array>

namespace mm::core {

namespace {

constexpr std::array<PatternNote, 4> kPlaceholderNotes{{
    {2 * 240, 360, 1, 45, 100},
    {6 * 240, 360, 1, 45, 100},
    {10 * 240, 360, 1, 45, 100},
    {14 * 240, 360, 1, 45, 124},
}};

} // namespace

PatternView placeholderPattern() {
    return {kPlaceholderNotes.data(), kPlaceholderNotes.size(), 4 * kTicksPerQuarter};
}

} // namespace mm::core
