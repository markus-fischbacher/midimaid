#pragma once

#include "core/Analysis.h"
#include "core/MidiImport.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm::core {

/// The reference set of a style (SPEC 3.20, v1.0: one set per style). Pure standard library: the plugin reads and
/// writes the files in the background and hands the text to these functions.

constexpr int kReferenceFormatVersion = 1;
constexpr size_t kMaxReferenceItems = 500; ///< per set
constexpr size_t kMinReferenceNotes = 4;   ///< a track with fewer notes is not a reference
constexpr int kMinReferenceWeight = 1;
constexpr int kMaxReferenceWeight = 5;
constexpr int kDefaultReferenceWeight = 3;

/// One track of an imported file: its notes exactly as written (4/4, at most 16 bars, SPEC 3.18), its role (suggested
/// at the import, the musician can correct it), the analysis (D-177, independent of the key) and the switches of the
/// set. `id` is the hash of the file and the channel of the track: a file is in the set once.
struct ReferenceItem {
    std::string id;
    std::string name; ///< file name, with the channel when the file had several tracks
    TrackRole role = TrackRole::Melody;
    double roleConfidence = 1.0; ///< how sure the detection was; 1 once the musician set the role
    bool active = true;
    int weight = kDefaultReferenceWeight;
    std::vector<std::string> tags; ///< the tags of the item in a set (only in the view of a set, v1.1)
    uint32_t lengthBars = 1;
    std::vector<MidiClipNote> notes;
    TrackAnalysis analysis;

    bool operator==(const ReferenceItem&) const;
};

struct ReferenceSet {
    std::string styleId;
    std::string setId;   ///< the id of the named set this is the view of (v1.1); empty for a set made by hand
    std::string setName; ///< its name; empty for the favorites
    std::vector<ReferenceItem> items;

    bool operator==(const ReferenceSet&) const;
};

/// What an import made of the files, for the status line of the view.
struct ReferenceImportReport {
    size_t added = 0;       ///< tracks that are new in the set
    size_t duplicates = 0;  ///< files (or tracks) that were in the set already
    size_t notFourFour = 0; ///< files with another time signature: skipped
    size_t unreadable = 0;  ///< files that are no usable MIDI file
    size_t tooSmall = 0;    ///< tracks with fewer than `kMinReferenceNotes` notes
    size_t overLimit = 0;   ///< tracks that did not fit into the set any more
    size_t truncated = 0;   ///< tracks longer than 16 bars: only the first 16 are kept

    bool operator==(const ReferenceImportReport&) const = default;
    ReferenceImportReport& operator+=(const ReferenceImportReport& other);
};

/// A 64-bit FNV-1a hash of the bytes with their length, as 24 hex digits (the key of the cache).
std::string fileHash(const uint8_t* data, size_t size);

/// The tracks of one MIDI file as set items: one item per channel with at least `kMinReferenceNotes` notes (a file
/// with several tracks is taken apart, SPEC 3.20); the analysis uses the scales and weights of `style`. `report` gets
/// the skipped tracks. An unreadable or non-4/4 file gives no items and a note in the report.
std::vector<ReferenceItem> itemsFromMidiFile(const std::string& fileName, const std::vector<uint8_t>& bytes,
                                             const StyleProfile& style, ReferenceImportReport& report);

/// True when an item that came from the file with this hash is in the set (no need to read and analyse it again).
bool containsFile(const ReferenceSet& set, const std::string& hash);

/// Adds the items that are not in the set yet (by id) up to `kMaxReferenceItems`; counts the rest in `report`.
void addItems(ReferenceSet& set, std::vector<ReferenceItem> items, ReferenceImportReport& report);

/// Changes of one item by id; false for an unknown id (nothing changes). The weight is clamped to 1 to 5.
bool setItemActive(ReferenceSet& set, const std::string& id, bool active);
bool setItemWeight(ReferenceSet& set, const std::string& id, int weight);
bool setItemRole(ReferenceSet& set, const std::string& id, TrackRole role);
bool removeItem(ReferenceSet& set, const std::string& id);

/// The items that count: active ones, optionally of one role.
size_t activeCount(const ReferenceSet& set);
size_t countOfRole(const ReferenceSet& set, TrackRole role);
/// The items whose role the detection was unsure of (below `kUnsureRoleConfidence`) and the musician has not set.
size_t unsureRoleCount(const ReferenceSet& set);
bool isRoleUnsure(const ReferenceItem& item);

std::string_view toString(TrackRole role);
std::optional<TrackRole> trackRoleFromString(std::string_view text);

/// The set as JSON. `referenceSetFromJson` never fails: bad text gives an empty set of `styleId`, a bad item is left
/// out (counted in `dropped` when given), unknown fields are ignored, a file of a later version loads as far as it is
/// understood.
std::string referenceSetToJson(const ReferenceSet& set);
ReferenceSet referenceSetFromJson(std::string_view text, const std::string& styleId, size_t* dropped = nullptr,
                                  size_t limit = kMaxReferenceItems);

} // namespace mm::core
