#pragma once

#include "core/Pattern.h"
#include "core/SlotBank.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// The library (SPEC 3.12, 9.2): one JSON file per entry, a pattern (one slot) or a set (all 16 slots). Pure standard
/// library: the plugin reads and writes the files in the background and hands the text to these functions.

/// Version of the entry file. Every change of the format raises it and gets a migration (SPEC 5).
constexpr int kLibraryEntryVersion = 1;
constexpr size_t kMaxLibraryNameBytes = 64;
constexpr size_t kMaxLibraryEntries = 5000;

enum class LibraryKind { Pattern, Set };

/// What the list shows: name, style, key, seed and prompt (SPEC 3.12). For a set these are the values of its first
/// filled slot.
struct LibraryEntryInfo {
    std::string id; ///< the file name without ".json": lower-case letters, digits, '-' and '_'
    LibraryKind kind = LibraryKind::Pattern;
    std::string name;
    std::string styleId;
    PitchClass root = 0;
    std::string scaleId;
    uint64_t seed = 0;
    std::string prompt;
    uint32_t lengthBars = 0;
    uint32_t slotCount = 1; ///< slots with a pattern (1 for a pattern)
    int64_t createdUnixMs = 0;

    bool operator==(const LibraryEntryInfo&) const = default;
};

struct LibraryEntry {
    LibraryEntryInfo info;
    std::optional<Pattern> pattern; ///< kind Pattern
    std::optional<SlotBank> set;    ///< kind Set: patterns, names and colours of the slots, no history
    std::optional<std::string> slotName; ///< kind Pattern: the name the slot had
    uint8_t slotColor = 0;
};

/// An id is the file name of the entry: 1 to 64 characters of [a-z0-9_-]. Nothing else reaches the file system.
bool isValidLibraryId(std::string_view id);

/// A name without control characters, trimmed, cut to `kMaxLibraryNameBytes` at a character boundary. An empty name
/// becomes `fallback` (sanitized the same way).
std::string sanitizeLibraryName(std::string_view name, std::string_view fallback = {});

/// An entry for one pattern; the info comes from the pattern. `slotName` and `slotColor` are what the slot had.
LibraryEntry makePatternEntry(std::string id, std::string_view name, const Pattern& pattern, std::string_view slotName,
                              uint8_t slotColor, int64_t createdUnixMs);
/// An entry for all slots. Without a filled slot: nullopt.
std::optional<LibraryEntry> makeSetEntry(std::string id, std::string_view name, const SlotBank& bank,
                                         int64_t createdUnixMs);

/// The entry as JSON text (sorted keys: equal entries give equal text).
std::string libraryEntryToString(const LibraryEntry& entry);

struct LibraryLoadResult {
    std::optional<LibraryEntry> entry;
    std::string error; ///< empty on success; otherwise path and reason
    std::vector<std::string> problems; ///< slots of a set that were left empty
    bool fromNewerVersion = false;

    bool ok() const { return entry.has_value(); }
};

/// Never throws and never crashes on bad input. The id is the one the file name gives (the id inside the text is
/// ignored, so a copied file cannot clash with another). Unknown fields are ignored.
LibraryLoadResult loadLibraryEntry(std::string_view text, std::string_view idFromFileName);

/// A Standard MIDI File of the pattern as the engine plays it (output stage of SPEC 4.2a, all voices on their MIDI
/// channels, one file). Nothing for a pattern without notes.
std::vector<uint8_t> exportPatternMidi(const Pattern& pattern, const StyleProfile& style, double bpm);

} // namespace mm::core
