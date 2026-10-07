#pragma once

#include "core/PlaybackPattern.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mm::engine {

using core::kTicksPerQuarter;
using core::PatternNote;
using core::PatternView;
using core::placeholderPattern;

/// Transport state of one audio block as reported by the host.
struct TransportInfo {
    bool hasPosition = false; // false when the host reports no PPQ position or tempo
    bool isPlaying = false;
    double ppq = 0.0; // position at the start of the block
    double bpm = 0.0;
    bool isLooping = false;
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
};

struct MidiEvent {
    int32_t sampleOffset;
    uint8_t channel;
    uint8_t pitch;
    uint8_t velocity;
    bool noteOn;
};

/// Fixed-capacity event list, filled by the player on the audio thread (no allocation).
class MidiEventList {
public:
    static constexpr size_t kCapacity = 1024;

    void clear() { size_ = 0; }
    size_t size() const { return size_; }
    bool full() const { return size_ >= kCapacity; }
    const MidiEvent& operator[](size_t i) const { return events_[i]; }
    const MidiEvent* begin() const { return events_.data(); }
    const MidiEvent* end() const { return events_.data() + size_; }

    /// Returns false when the list is full.
    bool push(const MidiEvent& event);
    /// Orders by sample offset, note-offs before note-ons at the same offset (stable).
    void sort();

private:
    std::array<MidiEvent, kCapacity> events_{};
    size_t size_ = 0;
};

/// Plays a pattern in sync with the host transport (SPEC 6.1, 6.2). No JUCE, no allocation, no locks.
///
/// Position = song position modulo pattern length (switch time is PPQ 0, as after a start or jump).
/// Every note-on gets a guaranteed note-off: on stop, jump (any PPQ discontinuity, including the loop wrap),
/// missing host data and bypass. A tempo change inside a block (the host reports the tempo at the block start
/// only) shifts the next block's position; that is no jump (SPEC 6.2, D-125).
class PatternPlayer {
public:
    explicit PatternPlayer(PatternView pattern = placeholderPattern());

    /// Replaces the pattern at once (no quantization, no note-offs): for tests and the initial pattern.
    void setPattern(PatternView pattern);

    /// Asks for a switch to `next` at the next quantization point (SPEC 6.1a, D-131). The point is the first
    /// multiple of `gridPpq` (rounded up to a bar line, v1.0 restarts on bars only) that is not before the start of
    /// the block that first sees the request; it is never in the past. Start and jumps apply the switch at once, a
    /// stop keeps it for the next start. A newer request replaces an older one.
    void requestSwitch(PatternView next, double gridPpq);
    /// Same, with a point planned by the hub (PPQ stamp). A stamp already behind the block start falls back to the
    /// next grid point.
    void requestSwitchAt(PatternView next, double stampPpq, double gridPpq);
    bool switchPending() const { return pending_.active; }

    /// Call from prepareToPlay. Keeps the active-note table: notes still sounding get their note-off in
    /// the first block afterwards (SPEC 6.2).
    void invalidateTransport();

    void process(const TransportInfo& transport, int numSamples, double sampleRate, MidiEventList& out);

    /// Note-off for everything sounding (stop, bypass). `out` is not cleared.
    void releaseAll(MidiEventList& out, int sampleOffset);

private:
    struct ActiveNote {
        bool active = false;
        double endPpq = 0.0;
        uint32_t startedInCall = 0;
        int32_t startOffset = 0;
    };

    /// Starts notes whose start lies in [scanFrom, to) and ends those that end before `to`. Sample offsets are
    /// measured from `origin` (the PPQ position of sample `sampleBase`); `scanFrom` differs from it when the
    /// host position deviates from the expected one (tempo change inside the previous block).
    void processSegment(double scanFrom, double origin, double to, int sampleBase, int numSamples, double ppqPerSample,
                        MidiEventList& out);
    /// Plays [scanFrom, to), cutting at the pending switch point when it lies inside.
    void playRange(double scanFrom, double origin, double to, int sampleBase, int numSamples, double ppqPerSample,
                   MidiEventList& out);
    void releaseAt(MidiEventList& out, int sampleOffset, bool guardOwnStarts);
    void startNote(const PatternNote& note, double endPpq, int offset, MidiEventList& out);

    struct PendingSwitch {
        PatternView pattern;
        double gridPpq = 4.0;
        double stampPpq = 0.0;
        bool hasStamp = false;
        bool active = false;
        bool resolved = false; // the point is fixed in the block that first sees the request
        double pointPpq = 0.0;
    };

    PatternView pattern_;
    PendingSwitch pending_;
    std::array<std::array<ActiveNote, 128>, 16> active_{};
    size_t activeCount_ = 0;
    bool playing_ = false;
    bool pendingWrapRelease_ = false;
    double expectedPpq_ = 0.0;
    double lastPpqPerSample_ = 0.0; // tempo and length of the previous block, for the tempo-change tolerance
    int lastNumSamples_ = 0;
    uint32_t call_ = 0;
};

} // namespace mm::engine
