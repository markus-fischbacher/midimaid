#pragma once

#include "core/PlaybackPattern.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

namespace mm::plugin {

/// Writes the exported .mid file for drag & drop on a background thread (SPEC 3.4).
/// The file lives in a per-instance folder below the temp directory and is removed when the instance goes away;
/// folders left behind by crashed sessions are cleaned up on the next start.
class MidiExporter {
public:
    MidiExporter();
    ~MidiExporter();

    /// The notes of one voice and the names of its file.
    struct Request {
        std::vector<mm::core::PatternNote> notes;
        uint32_t lengthTicks = 0;
        int voice = 1;         ///< 1 to `kMaxVoices`: which voice the file is of
        juce::String fileName; ///< MidiMaid_<slot>_<voice>.mid
        juce::String trackName;
    };
    /// What one export writes: a file per voice, all at the DAW tempo, and the voice that `readyFile()` returns.
    struct Batch {
        std::vector<Request> files;
        double bpm = 120.0;
        int primaryVoice = 1;
    };

    /// Non-blocking: writes the files in the background. Files of the export before that are not part of this one
    /// are removed afterwards (a voice that is gone, a slot that was left).
    void requestExport(Batch batch);
    /// Non-blocking: nothing to drag any more (the slot is empty): removes all files.
    void clear();

    /// The file of the primary voice once an export has finished, otherwise an empty path. Message thread.
    juce::File readyFile() const;
    /// The file of `voice` (1 to `kMaxVoices`), or an empty path when that voice was not exported.
    juce::File readyFile(int voice) const;

    /// Tempo of the files currently on disk (0 until the first export finished).
    double exportedBpm() const;

private:
    juce::File folder_;
    mutable juce::CriticalSection fileLock_; // guards the files, written by the worker and read on the message thread
    std::map<int, juce::File> files_;        // voice -> file of the last export
    int primaryVoice_ = 1;
    std::atomic<double> exportedBpm_{0.0};
    juce::ThreadPool pool_{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiExporter)
};

} // namespace mm::plugin
