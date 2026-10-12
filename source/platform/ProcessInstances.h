#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace mm::platform {

/// Counts of the plug-in instances per format in this process (SPEC 6.5, D-222). Every plug-in format is its own binary
/// with its own statics, so the registry cannot tell whether an AU hub and a VST3 voice live side by side; the process
/// environment is the one place all binaries of a process share. No threads are involved: instances are created and
/// destroyed on the message thread.
///
/// The state is a text like "au=2;vst3=1" (tags of letters and digits only).
std::string withInstanceCount(std::string_view state, std::string_view tag, int delta);
int instanceCount(std::string_view state, std::string_view tag);
/// Whether instances of a tag other than `tag` exist.
bool otherTagsPresent(std::string_view state, std::string_view tag);

/// The same on the environment variable of the process.
void registerInstance(std::string_view tag);
void unregisterInstance(std::string_view tag);
bool otherFormatsPresent(std::string_view tag);

/// Registers an instance for its lifetime.
class InstanceRegistration {
public:
    explicit InstanceRegistration(std::string tag) : tag_(std::move(tag)) {
        if (!tag_.empty()) {
            registerInstance(tag_);
        }
    }
    ~InstanceRegistration() {
        if (!tag_.empty()) {
            unregisterInstance(tag_);
        }
    }
    InstanceRegistration(const InstanceRegistration&) = delete;
    InstanceRegistration& operator=(const InstanceRegistration&) = delete;

private:
    std::string tag_;
};

} // namespace mm::platform
