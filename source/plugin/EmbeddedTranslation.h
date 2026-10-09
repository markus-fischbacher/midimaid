#pragma once

#include "core/Translation.h"

#include <juce_core/juce_core.h>

namespace mm::plugin {

/// The UI texts compiled into the plugin (`resources/i18n/de.json`). Built once, on first use; never changes.
const mm::core::Translation& embeddedTranslation();

/// The UI text for `key` (see `core/TextKeys.h`), with `{name}` placeholders filled from `args`.
juce::String tr(std::string_view key);
juce::String tr(std::string_view key, mm::core::Translation::Args args);

} // namespace mm::plugin
