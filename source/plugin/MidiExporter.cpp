#include "plugin/MidiExporter.h"

#include "core/MidiFile.h"
#include "core/PlaybackPattern.h"

namespace mm::plugin {

namespace {

juce::File exportRoot() {
    return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MidiMaid");
}

} // namespace

juce::String MidiExporter::fileName() {
    return "MidiMaid_Slot1_Bass.mid"; // MidiMaid_<slot name>_<voice>.mid; slots and voices follow later
}

MidiExporter::MidiExporter() {
    folder_ = exportRoot().getChildFile(juce::Uuid().toDashedString());
    file_ = folder_.getChildFile(fileName());

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

void MidiExporter::requestExport(double bpm) {
    pool_.addJob([this, bpm] {
        mm::core::MidiFileOptions options;
        options.bpm = bpm;
        options.trackName = "MidiMaid Bass";
        const auto bytes = mm::core::writeMidiFile(mm::core::placeholderPattern(), options);

        if (folder_.createDirectory().failed()) {
            return;
        }
        if (file_.replaceWithData(bytes.data(), bytes.size())) {
            exportedBpm_.store(bpm);
            ready_.store(true);
        }
    });
}

juce::File MidiExporter::readyFile() const {
    return ready_.load() ? file_ : juce::File();
}

double MidiExporter::exportedBpm() const {
    return exportedBpm_.load();
}

} // namespace mm::plugin
