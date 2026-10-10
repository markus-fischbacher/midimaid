#pragma once

#include "ai/AiTypes.h"

#include <juce_core/juce_core.h>

namespace mm::plugin {

/// The reason of a failed AI request in words of the UI (SPEC 7.9): the status of the provider and the HTTP code if
/// there was one. Never the text of the provider (it could hold anything).
juce::String aiStatusText(mm::ai::AiStatus status, int httpStatus);

/// The name of a schema level in the words of the UI.
juce::String schemaLevelText(mm::ai::SchemaSupport level);

} // namespace mm::plugin
