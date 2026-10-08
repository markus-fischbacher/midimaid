#include "plugin/MidiExporter.h"

#include "core/MidiFile.h"
#include "core/PlaybackPattern.h"

#include <algorithm>

namespace mm::plugin {

namespace {

juce::File exportRoot() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MidiMaid");
}

} // namespace

MidiExporter::MidiExporter() {
    folder_ = exportRoot().getChildFile(juce::Uuid().toDashedString());

    // Clean up folders of earlier sessions that were not removed (crash, kill) in the background.
    pool_.addJob([root = exportRoot(), own = folder_] {
        const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::days(1);
        for (const auto& entry : juce::RangedDirectoryIterator(root, false, "*", juce::File::findDirectories)) {
            const auto dir = entry.getFile();
            if (dir != own && dir.getCreationTime() < cutoff) {
                dir.deleteRecursively();
            }
        }
    });
}

MidiExporter::~MidiExporter() {
    pool_.removeAllJobs(true, 2000); // cancel and wait: no job touches the plugin after this
    // Remove our files on a short-lived worker so file access stays off the message thread.
    juce::ThreadPool cleanup(1);
    juce::WaitableEvent done;
    cleanup.addJob([folder = folder_, &done] {
        folder.deleteRecursively();
        done.signal();
    });
    done.wait(2000);
}

void MidiExporter::requestExport(Batch batch) {
    pool_.addJob([this, batch = std::move(batch)] {
        if (folder_.createDirectory().failed()) {
            return;
        }
        std::map<int, juce::File> written;
        for (const auto& request : batch.files) {
            mm::core::MidiFileOptions options;
            options.bpm = batch.bpm;
            options.trackName = request.trackName.toStdString();
            const mm::core::PatternView view{request.notes.data(), request.notes.size(), request.lengthTicks};
            const auto bytes = mm::core::writeMidiFile(view, options);
            const auto next = folder_.getChildFile(request.fileName);
            if (next.replaceWithData(bytes.data(), bytes.size())) {
                written[request.voice] = next;
            }
        }
        std::map<int, juce::File> previous;
        {
            const juce::ScopedLock lock(fileLock_);
            previous = std::move(files_);
            files_ = written;
            primaryVoice_ = batch.primaryVoice;
        }
        for (const auto& [voice, file] : previous) {
            const bool stillUsed =
                std::any_of(written.begin(), written.end(), [&](const auto& entry) { return entry.second == file; });
            if (!stillUsed) {
                file.deleteFile();
            }
        }
        exportedBpm_.store(batch.bpm);
    });
}

void MidiExporter::clear() {
    pool_.addJob([this] {
        std::map<int, juce::File> previous;
        {
            const juce::ScopedLock lock(fileLock_);
            previous = std::move(files_);
            files_.clear();
        }
        exportedBpm_.store(0.0);
        for (const auto& [voice, file] : previous) {
            file.deleteFile();
        }
    });
}

juce::File MidiExporter::readyFile() const {
    const juce::ScopedLock lock(fileLock_);
    const auto it = files_.find(primaryVoice_);
    return it != files_.end() ? it->second : juce::File();
}

juce::File MidiExporter::readyFile(int voice) const {
    const juce::ScopedLock lock(fileLock_);
    const auto it = files_.find(voice);
    return it != files_.end() ? it->second : juce::File();
}

double MidiExporter::exportedBpm() const {
    return exportedBpm_.load();
}

} // namespace mm::plugin
