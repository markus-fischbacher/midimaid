#pragma once

#include "core/Pattern.h"
#include "core/ReferenceSet.h"

#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// The references of one style as the musician keeps them (SPEC 3.20, v1.1, D-238): one **pool** of items (a file is in
/// it once) and any number of **named sets** that pick items from it, each with its own on/off switch, weight and tags
/// per item. One set is active; the automatic set "Favoriten" always exists. Pure standard library.
/// Everything that reads the references (generation, copy protection, AI examples, the view) takes the **view** of one
/// set, a `ReferenceSet`, so the v1.0 functions work unchanged.

inline const char* const kFavoritesSetId = "favorites";
constexpr size_t kMaxReferenceSets = 24;
constexpr size_t kMaxReferenceSetNameBytes = 40;
constexpr size_t kMaxPoolItems = 2000;
constexpr size_t kMaxMemberTags = 6;
constexpr int kFavoriteMinRating = 4; ///< a library pattern with this many stars or more is a favorite
inline const char* const kFavoriteIdPrefix = "fav:";

struct ReferenceMember {
    std::string id; ///< the id of a pool item
    bool active = true;
    int weight = kDefaultReferenceWeight;
    std::vector<std::string> tags;

    bool operator==(const ReferenceMember&) const = default;
};

struct NamedReferenceSet {
    std::string id;   ///< "favorites", or "s1", "s2" ... ("default" for the set of a v1.0 file)
    std::string name; ///< empty for the favorites (the view shows the translated name)
    std::vector<ReferenceMember> members;

    bool operator==(const NamedReferenceSet&) const = default;
};

struct ReferenceCollection {
    std::string styleId;
    std::vector<ReferenceItem> pool; ///< `active` and `weight` of these are not used: the sets hold them
    std::vector<NamedReferenceSet> sets;
    std::string activeSetId; ///< the id of one of `sets` (empty only when there are none)

    bool operator==(const ReferenceCollection&) const;
};

/// The favorites set exists, the active set is one of the sets, no member points to a missing item, no item is in no
/// set (it would be lost), the ids are unique. Called after every change and after reading.
void normalize(ReferenceCollection& collection);

const NamedReferenceSet* findSet(const ReferenceCollection& collection, std::string_view setId);

/// The set as the view that every consumer takes: the items in the order of the set with the switches of the set.
/// `setName` is empty for the favorites and for an unknown set (which gives an empty view with that id).
ReferenceSet viewOf(const ReferenceCollection& collection, std::string_view setId);
ReferenceSet activeView(const ReferenceCollection& collection);

/// A clean name: no control characters, trimmed, at most `kMaxReferenceSetNameBytes` bytes cut at a character.
std::string cleanSetName(std::string_view name);

/// Creates a set and returns its id; "" for an empty name, a name another set has (any case) or too many sets.
std::string createSet(ReferenceCollection& collection, std::string_view name);
/// Not for the favorites; the same name rules. False on failure.
bool renameSet(ReferenceCollection& collection, std::string_view setId, std::string_view name);
/// Not for the favorites. The items that are in no other set go with it. The active set falls back to the first one.
bool deleteSet(ReferenceCollection& collection, std::string_view setId);
bool setActiveSet(ReferenceCollection& collection, std::string_view setId);

/// Changes of one member of one set (false for an unknown set or item). The weight is clamped to 1 to 5; the tags
/// are cleaned like the tags of the library and cut to `kMaxMemberTags`.
bool setMemberActive(ReferenceCollection& collection, std::string_view setId, std::string_view itemId, bool active);
bool setMemberWeight(ReferenceCollection& collection, std::string_view setId, std::string_view itemId, int weight);
bool setMemberTags(ReferenceCollection& collection, std::string_view setId, std::string_view itemId,
                   std::vector<std::string> tags);
/// The role belongs to the item and counts in every set.
bool setItemRole(ReferenceCollection& collection, std::string_view itemId, TrackRole role);

/// Puts a pool item into a set (false when it is in it already, or unknown).
bool addMember(ReferenceCollection& collection, std::string_view setId, std::string_view itemId);
/// Takes it out of the set; an item that is in no set any more leaves the pool.
bool removeMember(ReferenceCollection& collection, std::string_view setId, std::string_view itemId);

/// True when an item that came from the file with this hash is in the pool (no need to analyse it again).
bool poolContainsFile(const ReferenceCollection& collection, const std::string& hash);
/// Puts the items of a known file into the set (the cache of D-179): counted as added, or as duplicate when the set has
/// them already.
void addKnownFile(ReferenceCollection& collection, std::string_view setId, const std::string& hash,
                  ReferenceImportReport& report);
/// New items go into the pool and the set; an item the pool has goes into the set only; counts the rest as in
/// `addItems` (`overLimit` when the set or the pool is full).
void addItems(ReferenceCollection& collection, std::string_view setId, std::vector<ReferenceItem> items,
              ReferenceImportReport& report);

/// ---- favorites ----

/// The ids of the items of one library entry: `fav:<entryId>:<voice index>`.
std::string favoriteItemId(std::string_view entryId, size_t voiceIndex);
bool isFavoriteItem(std::string_view itemId);

/// One item per voice of the pattern that has at least `kMinReferenceNotes` notes (the role from the voice, the
/// analysis with the scales of `style`, the notes as written without ratchets and conditions).
std::vector<ReferenceItem> favoriteItemsOf(const Pattern& pattern, std::string_view entryId, const std::string& name,
                                           const StyleProfile& style);
/// Replaces the items of the entry (the pool and its sets) by `items` and puts them into the favorites and, with
/// `alsoActive`, into the active set. An empty `items` removes the entry.
void setFavorite(ReferenceCollection& collection, std::string_view entryId, std::vector<ReferenceItem> items,
                 bool alsoActive);
/// Removes every item of the entry from the pool and from all sets.
void removeFavorite(ReferenceCollection& collection, std::string_view entryId);

/// ---- files ----

/// The collection as JSON (version 2). `referenceCollectionFromJson` never fails: bad text gives an empty collection of
/// `styleId`; a file of version 1 (one set, `referenceSetFromJson`) becomes a collection with the set "default"; bad
/// items and members are dropped (counted in `dropped` when given); unknown fields are ignored.
std::string referenceCollectionToJson(const ReferenceCollection& collection);
ReferenceCollection referenceCollectionFromJson(std::string_view text, const std::string& styleId,
                                                size_t* dropped = nullptr);

} // namespace mm::core
