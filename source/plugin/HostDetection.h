#pragma once

#include "plugin/HostCapabilities.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// The only place that reads JUCE's `wrapperType` and `PluginHostType` (SPEC 2.4).
/// Call after the processor is fully constructed (it queries `isMidiEffect()`).
HostCapabilities detectHostCapabilities(const juce::AudioProcessor& processor);

} // namespace mm::plugin
