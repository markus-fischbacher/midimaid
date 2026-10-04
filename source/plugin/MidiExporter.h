#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

namespace mm::plugin {

/// Writes the exported .mid file for drag & drop on a background thread (SPEC 3.4).
/// The file lives in a per-instance folder below the temp directory and is removed when the instance goes away;
/// folders left behind by crashed sessions are cleaned up on the next start.
class MidiExporter {
public:
    MidiExporter();
    ~MidiExporter();

    /// Non-blocking: writes the file with the given DAW tempo in the background.
    void requestExport(double bpm);

    /// The file once the first export has finished, otherwise an empty path. Message thread.
    juce::File readyFile() const;

    /// Tempo of the file currently on disk (0 until the first export finished).
    double exportedBpm() const;

    static juce::String fileName();

private:
    juce::File folder_;
    juce::File file_;
    std::atomic<bool> ready_{false};
    std::atomic<double> exportedBpm_{0.0};
    juce::ThreadPool pool_{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiExporter)
};

} // namespace mm::plugin
