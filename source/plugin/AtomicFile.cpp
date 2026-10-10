#include "plugin/AtomicFile.h"

namespace mm::plugin {

bool writeFileAtomically(const juce::File& target, const std::string& text) {
    const auto folder = target.getParentDirectory();
    if (!folder.createDirectory().wasOk()) {
        return false;
    }
    const auto temporary =
        folder.getChildFile("." + target.getFileName() + "." + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()) +
                            kTemporarySuffix);
    const bool written = [&] {
        juce::FileOutputStream stream(temporary);
        if (stream.failedToOpen()) {
            return false;
        }
        stream.setPosition(0);
        stream.truncate();
        const bool ok = stream.write(text.data(), text.size());
        stream.flush();
        return ok && !stream.getStatus().failed();
    }();
    if (!written) {
        temporary.deleteFile();
        return false;
    }
    if (!temporary.replaceFileIn(target)) {
        temporary.deleteFile();
        return false;
    }
    return true;
}

int removeStaleTemporaries(const juce::File& folder) {
    int removed = 0;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, false, "*" + juce::String(kTemporarySuffix))) {
        removed += file.deleteFile() ? 1 : 0;
    }
    return removed;
}

} // namespace mm::plugin
