#pragma once

#include "core/GrooveLibrary.h"

namespace mm::plugin {

/// The groove templates compiled into the plugin (`resources/grooves`, SPEC 3.9). Built once, on first use; never changes.
const mm::core::GrooveLibrary& embeddedGrooves();

} // namespace mm::plugin
