#pragma once

#include "plugin/GlobalSettingsStore.h"

namespace mmtest {

/// A settings store without file: the glue tests run with it, so that they neither read nor write the user's own
/// settings file (a listener installs it for the whole run, see glue_listener.cpp).
inline mm::plugin::GlobalSettingsStore& disabledSettings() {
    static mm::plugin::GlobalSettingsStore store{juce::File()};
    return store;
}

} // namespace mmtest
