#pragma once

#include "plugin/EmbeddedStyles.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/ReferenceStore.h"

namespace mmtest {

/// A settings store without file: the glue tests run with it, so that they neither read nor write the user's own
/// settings file (a listener installs it for the whole run, see glue_listener.cpp).
inline mm::plugin::GlobalSettingsStore& disabledSettings() {
    static mm::plugin::GlobalSettingsStore store{juce::File()};
    return store;
}

/// The same for the reference sets: a store without folder, in memory only.
inline mm::plugin::ReferenceStore& disabledReferences() {
    static mm::plugin::ReferenceStore store{juce::File(), mm::plugin::embeddedStyles()};
    return store;
}

} // namespace mmtest
