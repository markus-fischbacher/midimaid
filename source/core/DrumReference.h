#pragma once

#include "core/MidiImport.h"
#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// The drum reference of a pattern (SPEC 3.14). Pure standard library.

enum class DrumRole { Kick, Snare, Clap, ClosedHat, OpenHat, Ride, Other };

std::string_view toString(DrumRole role);
std::optional<DrumRole> drumRoleFromString(std::string_view text);

/// Which note is which drum: General MIDI (kick 36, snare 38, clap 39, closed hat 42, open hat 46, ride 51) unless the
/// musician maps it otherwise (a drum rack). The first entry for a note wins; a note nobody names is `Other`.
struct DrumMapping {
    struct Entry {
        uint8_t note = 0;
        DrumRole role = DrumRole::Other;
        bool operator==(const Entry&) const = default;
    };
    std::vector<Entry> overrides;

    DrumRole roleOf(uint8_t note) const;
    bool operator==(const DrumMapping&) const = default;
};

/// The mapping as the line of the settings, "36=kick, 49=open_hat" (entries with an unknown role, a note above 127 or
/// a double note are left out), and back.
std::string drumMappingToText(const DrumMapping& mapping);
DrumMapping drumMappingFromText(std::string_view text);

/// What the drums of a clip give (SPEC 3.14): the kick steps (custom kick grid), the steps with a hat and for each its
/// mean timing offset and velocity (groove), and the accents (open hats, hats that are clearly louder than the rest).
/// The reference is one or two bars; of a longer clip the steps count that carry the notes in at least half of the
/// repeats. Starts are assigned to the nearest 16th. nullopt when the clip has neither kick nor hat.
std::optional<RhythmReference> deriveRhythmReference(const std::vector<MidiClipNote>& notes, uint32_t lengthBars,
                                                     const DrumMapping& mapping = {});

/// Puts a reference into the pattern (SPEC 3.14): kick grid `custom`, the groove of every voice that plays generated
/// material follows the reference (no swing of its own, D-179), then the constraint layer runs for the new kick. Locked
/// voices keep their notes (the constraint layer leaves them alone); a voice with an imported line keeps its groove
/// settings. The notes of the other voices adapt to the kick, they are not generated again.
void applyRhythmReference(Pattern& pattern, const RhythmReference& reference, const StyleProfile& style);

/// Takes the reference out again: the default kick grid of the style and the straight groove of the style.
void removeRhythmReference(Pattern& pattern, const StyleProfile& style);

} // namespace mm::core
