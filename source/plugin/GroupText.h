#pragma once

#include "core/GroupRegistry.h"

#include <cstddef>
#include <string>

namespace mm::plugin {

/// The group line of the editor (SPEC 6.5). Texts from the translation table.
/// A voice that finds no hub also says why that can be a host setting: hosts that run plug-ins in separate processes
/// (Bitwig "Individually") keep the instances from seeing each other, and the plug-in cannot tell that apart from a
/// missing hub.
std::string groupStatusText(mm::core::GroupStatus status, size_t voices);

} // namespace mm::plugin
