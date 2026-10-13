#include "plugin/Favorites.h"

namespace mm::plugin {

void updateFavorite(LibraryStore& library, ReferenceStore& references, const std::string& entryId, bool alsoActive) {
    const auto info = library.info(entryId);
    if (!info || info->kind != mm::core::LibraryKind::Pattern || info->rating < mm::core::kFavoriteMinRating) {
        references.removeFavorite(entryId);
        return;
    }
    const std::string styleId = info->styleId;
    const std::string name = info->name;
    library.load(entryId, [&references, entryId, styleId, name, alsoActive](mm::core::LibraryLoadResult loaded) {
        if (loaded.ok() && loaded.entry->pattern) {
            references.setFavorite(styleId, entryId, name, *loaded.entry->pattern, alsoActive);
        }
    });
}

} // namespace mm::plugin
