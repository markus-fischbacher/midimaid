#include "plugin/AiText.h"

#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

namespace keys = mm::core::text;

juce::String aiStatusText(mm::ai::AiStatus status, int httpStatus) {
    using mm::ai::AiStatus;
    std::string_view key = keys::kAiReasonNetwork;
    switch (status) {
    case AiStatus::AuthFailed:
        key = keys::kAiReasonAuth;
        break;
    case AiStatus::ModelNotFound:
        key = keys::kAiReasonModel;
        break;
    case AiStatus::RateLimited:
        key = keys::kAiReasonRate;
        break;
    case AiStatus::ServerError:
        key = keys::kAiReasonServer;
        break;
    case AiStatus::BadRequest:
        key = keys::kAiReasonBad;
        break;
    case AiStatus::Timeout:
        key = keys::kAiReasonTimeout;
        break;
    case AiStatus::NetworkError:
    case AiStatus::Cancelled:
    case AiStatus::Ok:
        break;
    }
    auto text = tr(key);
    if (httpStatus != 0) {
        text += " (HTTP " + juce::String(httpStatus) + ")";
    }
    return text;
}

juce::String schemaLevelText(mm::ai::SchemaSupport level) {
    switch (level) {
    case mm::ai::SchemaSupport::Enforced:
        return tr(keys::kLevelEnforced);
    case mm::ai::SchemaSupport::JsonOnly:
        return tr(keys::kLevelJson);
    case mm::ai::SchemaSupport::PromptOnly:
        return tr(keys::kLevelPrompt);
    }
    return {};
}

} // namespace mm::plugin
