#pragma once

#include "plugin/LibraryStore.h"
#include "plugin/ReferenceStore.h"

#include <string>

namespace mm::plugin {

/// Keeps the automatic reference set "Favoriten" in step with the library (SPEC 3.20, D-238): an entry with
/// `kFavoriteMinRating` stars or more puts its voices into the favorites (and, with `alsoActive`, into the active set
/// of its style); every other state of the entry takes them out of every set. The pattern is read in the background;
/// the sets change on the message thread. Both stores must outlive the call. An entry that is a set of slots has no
/// single pattern and is no favorite.
void updateFavorite(LibraryStore& library, ReferenceStore& references, const std::string& entryId, bool alsoActive);

} // namespace mm::plugin
