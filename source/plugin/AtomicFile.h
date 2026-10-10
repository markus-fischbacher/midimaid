#pragma once

#include <juce_core/juce_core.h>
#include <string>

namespace mm::plugin {

/// Suffix of the files that are being written. A crash leaves such a file behind, never a half-written target.
inline const char* const kTemporarySuffix = ".mmtmp";

/// Writes `text` next to `target` under a temporary name (`.<name>.<random>.mmtmp`) and then replaces `target` with it
/// (SPEC 9.2: temp + rename; the replacement is atomic where the file system allows it). The folder is created. False
/// when anything failed; `target` is then as it was, and the temporary file is gone. Any thread except the audio thread.
bool writeFileAtomically(const juce::File& target, const std::string& text);

/// Deletes the temporary files that crashed writes left in `folder` (not below it). Returns how many.
int removeStaleTemporaries(const juce::File& folder);

} // namespace mm::plugin
