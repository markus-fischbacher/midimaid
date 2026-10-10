#include "plugin/StateMigration.h"

namespace mm::plugin {

StateMigrationResult migrateState(juce::XmlElement& root, int currentVersion, std::span<const StateMigration> steps) {
    StateMigrationResult result;
    int version = root.getIntAttribute("stateVersion", 0);
    if (version < 1) {
        result.error = "stateVersion: missing or below 1";
        return result;
    }
    if (version > currentVersion) {
        result.ok = true;
        result.fromNewer = true;
        return result;
    }
    if (static_cast<size_t>(currentVersion - 1) > steps.size()) {
        result.error = "stateVersion: no migration to version " + std::to_string(currentVersion);
        return result;
    }
    for (; version < currentVersion; ++version) {
        const auto& step = steps[static_cast<size_t>(version - 1)];
        if (!step || !step(root, result.error)) {
            if (result.error.empty()) {
                result.error = "stateVersion: migration from version " + std::to_string(version) + " failed";
            }
            return result;
        }
        root.setAttribute("stateVersion", version + 1);
    }
    result.ok = true;
    return result;
}

std::span<const StateMigration> stateMigrations() {
    return {};
}

} // namespace mm::plugin
