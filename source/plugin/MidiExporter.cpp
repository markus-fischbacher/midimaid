#include "plugin/MidiExporter.h"

#include "core/MidiFile.h"
#include "core/PlaybackPattern.h"

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

void MidiExporter::requestExport(Request request) {
    pool_.addJob([this, request = std::move(request)] {
        mm::core::MidiFileOptions options;
        options.bpm = request.bpm;
        options.trackName = request.trackName.toStdString();
        const mm::core::PatternView view{request.notes.data(), request.notes.size(), request.lengthTicks};
        const auto bytes = mm::core::writeMidiFile(view, options);

        if (folder_.createDirectory().failed()) {
            return;
        }
        const auto next = folder_.getChildFile(request.fileName);
        if (!next.replaceWithData(bytes.data(), bytes.size())) {
            return;
        }
        juce::File previous;
        {
            const juce::ScopedLock lock(fileLock_);
            previous = file_;
            file_ = next;
        }
        if (previous != juce::File() && previous != next) {
            previous.deleteFile();
        }
        exportedBpm_.store(request.bpm);
        ready_.store(true);
    });
}

void MidiExporter::clear() {
    pool_.addJob([this] {
        juce::File previous;
        {
            const juce::ScopedLock lock(fileLock_);
            previous = file_;
            file_ = juce::File();
        }
        ready_.store(false);
        exportedBpm_.store(0.0);
        if (previous != juce::File()) {
            previous.deleteFile();
        }
    });
}

juce::File MidiExporter::readyFile() const {
    const juce::ScopedLock lock(fileLock_);
    return ready_.load() ? file_ : juce::File();
}

double MidiExporter::exportedBpm() const {
    return exportedBpm_.load();
}

} // namespace mm::plugin
