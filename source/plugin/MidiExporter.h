#pragma once

#include "core/PlaybackPattern.h"

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace mm::plugin {

/// Writes the exported .mid file for drag & drop on a background thread (SPEC 3.4).
/// The file lives in a per-instance folder below the temp directory and is removed when the instance goes away;
/// folders left behind by crashed sessions are cleaned up on the next start.
class MidiExporter {
public:
    MidiExporter();
    ~MidiExporter();

    /// What to write: the notes one instance plays, the DAW tempo and the names.
    struct Request {
        std::vector<mm::core::PatternNote> notes;
        uint32_t lengthTicks = 0;
        double bpm = 120.0;
        juce::String fileName; ///< MidiMaid_<slot>_<voice>.mid
        juce::String trackName;
    };

    /// Non-blocking: writes the file in the background; the file of the request before is removed afterwards.
    void requestExport(Request request);
    /// Non-blocking: nothing to drag any more (the slot is empty): removes the file.
    void clear();

    /// The file once the first export has finished, otherwise an empty path. Message thread.
    juce::File readyFile() const;

    /// Tempo of the file currently on disk (0 until the first export finished).
    double exportedBpm() const;

private:
    juce::File folder_;
    mutable juce::CriticalSection fileLock_; // guards `file_`, written by the worker and read on the message thread
    juce::File file_;
    std::atomic<bool> ready_{false};
    std::atomic<double> exportedBpm_{0.0};
    juce::ThreadPool pool_{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiExporter)
};

} // namespace mm::plugin
