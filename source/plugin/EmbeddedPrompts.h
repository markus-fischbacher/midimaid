#pragma once

#include "ai/PromptBuilder.h"

namespace mm::plugin {

/// The prompt templates compiled into the plugin (`resources/prompts/v1`). Built once, on first use; never changes.
const mm::ai::PromptTemplates& embeddedPrompts();

} // namespace mm::plugin
