#include "plugin/GroupText.h"

namespace mm::plugin {

namespace {
constexpr const char* kSandboxHint =
    " (if the hub is in this project: the host may run plug-ins in separate processes)";
}

std::string groupStatusText(mm::core::GroupStatus status, size_t voices) {
    using mm::core::GroupStatus;
    switch (status) {
    case GroupStatus::Hub:
        return "Hub: " + std::to_string(voices) + (voices == 1 ? " voice" : " voices");
    case GroupStatus::VoiceConnected:
        return "Connected to the hub";
    case GroupStatus::HubMissing:
        return std::string("Hub not found") + kSandboxHint;
    case GroupStatus::HubOffered:
        return std::string("Hub not found: you can take over") + kSandboxHint;
    case GroupStatus::HubRefused:
        return "There is already a hub";
    case GroupStatus::Solo:
        break;
    }
    return {};
}

} // namespace mm::plugin
