#pragma once

#include "core/StyleLibrary.h"

namespace mm::plugin {

/// The style profiles compiled into the plugin (`resources/styles`). Built once, on first use; never changes.
const mm::core::StyleLibrary& embeddedStyles();

} // namespace mm::plugin
