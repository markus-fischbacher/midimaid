#pragma once

#include "core/Library.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// The index of the library (SPEC 9.2, D-185): the list of the entries with what the list shows, so that the plugin
/// does not have to read every entry file at the start. Every record remembers the size and the modification time of
/// the file it was made from; a file that still has both is trusted, any other file is read again. The index is only a
/// cache: without it, or with a bad one, the library is rebuilt from the files. Pure standard library.

constexpr int kLibraryIndexVersion = 1;

struct LibraryIndexRecord {
    LibraryEntryInfo info;
    int64_t fileSize = 0;
    int64_t modifiedMs = 0; ///< modification time of the file, Unix ms

    bool operator==(const LibraryIndexRecord&) const = default;
};

/// What the folder holds for one entry file.
struct LibraryFileState {
    std::string id;
    int64_t fileSize = 0;
    int64_t modifiedMs = 0;

    bool operator==(const LibraryFileState&) const = default;
};

/// The records as JSON text (sorted by id, sorted keys: the same records give the same text).
std::string libraryIndexToString(std::vector<LibraryIndexRecord> records);

/// Never throws. Text that is no index (empty, not JSON, wrong shape, a version below 1 or above this one) gives
/// `ok == false` and no records; a record that is not usable (no valid id, wrong types) is left out. A newer index
/// version is not read: the next write replaces it.
struct LibraryIndexLoad {
    bool ok = false;
    std::vector<LibraryIndexRecord> records;
    size_t skipped = 0; ///< records left out
};
LibraryIndexLoad loadLibraryIndex(std::string_view text);

struct LibraryReconcile {
    std::vector<LibraryIndexRecord> trusted; ///< records whose file is unchanged
    std::vector<std::string> toRead;         ///< files that are new or changed, to read again (sorted by id)
    size_t dropped = 0;                      ///< records whose file is gone
};

/// Compares the index with the folder. A record counts when a file of its id has the same size and time; a record with
/// another id twice keeps the first. Files without a record are `toRead`.
LibraryReconcile reconcileLibraryIndex(const std::vector<LibraryIndexRecord>& records,
                                       const std::vector<LibraryFileState>& files);

} // namespace mm::core
