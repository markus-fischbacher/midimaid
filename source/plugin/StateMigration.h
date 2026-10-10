#pragma once

#include <functional>
#include <juce_core/juce_core.h>
#include <span>
#include <string>

namespace mm::plugin {

/// Version of the plugin state (SPEC 9.1); every change of its format raises it and gets a migration.
constexpr int kStateVersion = 1;

/// One step upgrades the state document by exactly one version (steps[0]: 1 -> 2, steps[1]: 2 -> 3, ...). It changes
/// the element in place and returns false with a reason when it cannot (SPEC 9.1: every change of the state format
/// raises `stateVersion` and gets a migration).
using StateMigration = std::function<bool(juce::XmlElement& root, std::string& error)>;

struct StateMigrationResult {
    bool ok = false;
    bool fromNewer = false; ///< written by a newer plugin version: left as it is, to be loaded as far as understood
    std::string error;      ///< `!ok`: why (the state is not used)
};

/// Brings `root` (the `MidiMaid` element) to `currentVersion`: an older document runs through the steps and gets the
/// new `stateVersion`; a document of the current version is left alone; a newer one is left alone and reported. A
/// missing or non-positive version, a missing step or a failing step gives `ok == false`; `root` may then be half
/// migrated and must not be used.
StateMigrationResult migrateState(juce::XmlElement& root, int currentVersion, std::span<const StateMigration> steps);

/// The steps of this plugin version. Empty while the state format is at version 1.
std::span<const StateMigration> stateMigrations();

} // namespace mm::plugin
