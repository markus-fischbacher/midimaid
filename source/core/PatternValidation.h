#pragma once

#include "core/Pattern.h"

#include <string>
#include <vector>

namespace mm::core {

/// Checks the invariants of SPEC 3.5, 3.7, 4.2 and 5. Returns one message per violation, each starting with the path of
/// the offending field (e.g. "voices[0].notes[2].pitch: ..."); an empty list means the pattern is valid.
std::vector<std::string> validatePattern(const Pattern& pattern);

} // namespace mm::core
