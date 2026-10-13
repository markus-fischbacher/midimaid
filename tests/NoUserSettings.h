#pragma once

#include "plugin/EmbeddedGrooves.h"
#include "plugin/EmbeddedStyles.h"
#include "plugin/GlobalSettingsStore.h"
#include "plugin/ReferenceStore.h"
#include "plugin/UserGrooveStore.h"

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

/// The same for the groove templates of the user: the embedded ones only, nothing is written.
inline mm::plugin::UserGrooveStore& disabledUserGrooves() {
    static mm::plugin::UserGrooveStore store{juce::File(), mm::plugin::embeddedGrooves()};
    return store;
}

} // namespace mmtest
