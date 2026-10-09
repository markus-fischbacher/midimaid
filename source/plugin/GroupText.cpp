#include "plugin/GroupText.h"

#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

namespace mm::plugin {

namespace {
namespace keys = mm::core::text;
std::string text(std::string_view key) {
    return embeddedTranslation().tr(key);
}
std::string text(std::string_view key, mm::core::Translation::Args args) {
    return embeddedTranslation().tr(key, args);
}
} // namespace

std::string groupStatusText(mm::core::GroupStatus status, size_t voices) {
    using mm::core::GroupStatus;
    switch (status) {
    case GroupStatus::Hub:
        return voices == 1 ? text(keys::kGroupHubOne) : text(keys::kGroupHubMany, {{"n", std::to_string(voices)}});
    case GroupStatus::VoiceConnected:
        return text(keys::kGroupConnected);
    case GroupStatus::HubMissing:
        return text(keys::kGroupMissing) + text(keys::kGroupSandboxHint);
    case GroupStatus::HubOffered:
        return text(keys::kGroupOffered) + text(keys::kGroupSandboxHint);
    case GroupStatus::HubRefused:
        return text(keys::kGroupRefused);
    case GroupStatus::Solo:
        break;
    }
    return {};
}

} // namespace mm::plugin
