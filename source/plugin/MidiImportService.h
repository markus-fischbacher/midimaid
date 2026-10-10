#pragma once

#include "core/MidiImport.h"

#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>

namespace mm::plugin {

/// A MIDI file that was dropped on a voice (SPEC 3.18).
struct ImportRequest {
    juce::File file;
    size_t voice = 0; ///< 0-based voice of the pattern the clip is meant for
    int slot = 0;     ///< 0-based slot, kept from the moment of the drop
};

struct ImportOutcome {
    bool unreadable = false;                                       ///< the file could not be read at all
    mm::core::MidiReadError error = mm::core::MidiReadError::None; ///< what was wrong with the MIDI data
    mm::core::ImportPlan plan;                                     ///< valid when `error` is none
};

/// Reads and checks dropped MIDI files on a background thread (CLAUDE.md: file access never on the message thread).
/// One worker; a new request replaces the one that is waiting or running (only the newest one delivers). The result
/// arrives on the message thread. The destructor waits, and nothing is delivered afterwards.
class MidiImportService {
public:
    using Delivery = std::function<void(const ImportRequest&, ImportOutcome)>;
    explicit MidiImportService(Delivery delivery);
    ~MidiImportService();

    /// Message thread.
    void request(const ImportRequest& request);
    /// True from the request until its result was delivered or dropped. Message thread.
    bool busy() const;

private:
    struct State;
    std::shared_ptr<State> state_;
    std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
    juce::ThreadPool pool_{1};
};

} // namespace mm::plugin
