#pragma once

#include "core/PlaybackPattern.h"
#include "engine/PatternHandover.h"

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
    ~PatternPlayer();
    PatternPlayer(const PatternPlayer&) = delete;
    PatternPlayer& operator=(const PatternPlayer&) = delete;

    /// Puts `pattern` into the selected slot and plays it at once (no quantization, no note-offs): for tests and the
    /// initial pattern. Not while the audio thread runs.
    void setPattern(PatternView pattern);

    /// Connects the lock-free handover (SPEC 6.3). The player takes results and edits at the start of each block
    /// and retires replaced patterns through the handover's return queue; it never frees one itself.
    void attach(PatternHandover* handover) { handover_ = handover; }

    /// The slot parameter, 1 to 16 (values outside are limited). Read at the block start: a change plays at the
    /// next grid point (SPEC 6.1a), after a start or jump at once (SPEC 3.13). Call from the audio thread before
    /// `process`. Slot 1 holds the initial pattern, the others are empty (silent).
    void setSlot(int slot);
    int selectedSlot() const { return selected_ + 1; }
    /// Grid of slot changes in PPQ (default one bar; rounded up to a bar line).
    void setSlotGrid(double gridPpq) { slotGridPpq_ = gridPpq; }

    /// Puts `next` into the selected slot like a result from the handover (no owner): it plays at the next grid
    /// point (SPEC 6.1a, D-131). The point is the first multiple of `gridPpq` (rounded up to a bar line, v1.0
    /// restarts on bars only) not before the start of the block that first sees the change; it is never in the
    /// past. Start and jumps apply it at once, a stop keeps it for the next start.
    void requestSwitch(PatternView next, double gridPpq);
    /// Same, with a point planned by the hub (PPQ stamp). A stamp already behind the block start falls back to the
    /// next grid point.
    void requestSwitchAt(PatternView next, double stampPpq, double gridPpq);
    /// True while the playing pattern differs from the selected slot's pattern (a change is waiting for its point).
    bool switchPending() const { return playingSlot_ != selected_; }

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
        uint32_t startTick = 0; // position of the note in its pattern, to recognise it after an edit
        uint32_t lengthTicks = 0;
    };

    /// Starts notes whose start lies in [scanFrom, to) and ends those that end before `to`. Sample offsets are
    /// measured from `origin` (the PPQ position of sample `sampleBase`); `scanFrom` differs from it when the
    /// host position deviates from the expected one (tempo change inside the previous block).
    void processSegment(double scanFrom, double origin, double to, int sampleBase, int numSamples, double ppqPerSample,
                        MidiEventList& out);
    /// Plays [scanFrom, to), cutting at the pending switch point when it lies inside.
    void playRange(double scanFrom, double origin, double to, int sampleBase, int numSamples, double ppqPerSample,
                   MidiEventList& out);
    void pollHandover(MidiEventList& out);
    /// Stores a result in `slot` (the pattern playing from that slot becomes "detached" and keeps playing).
    void storeResult(size_t slot, OwnedPattern* result);
    /// Replaces the pattern of `slot`; when it is the playing one, note changes apply at once.
    void storeEdit(size_t slot, OwnedPattern* edit, MidiEventList& out);
    bool canStoreResult(size_t slot) const;
    bool canStoreEdit(size_t slot) const;
    /// True when the pattern that stops playing at a commit can be handed back (or there is none to hand back).
    bool canRetire() const;
    /// Plays the selected slot's pattern now. Requires `canRetire()`.
    void commitSelected();
    /// Hands `pattern` back for freeing (the handover) or, without one, frees it (message-thread use in tests).
    void retire(OwnedPattern* pattern);
    static void dispose(OwnedPattern* pattern);
    void releaseAt(MidiEventList& out, int sampleOffset, bool guardOwnStarts);
    void startNote(const PatternNote& note, double endPpq, int offset, MidiEventList& out);
    void reportActive() const;

    struct SlotEntry {
        PatternView view;
        OwnedPattern* owner = nullptr; // set when the pattern came through the handover
    };

    /// Timing of the waiting change. The point is fixed in the block that first sees it, never in the past.
    struct SwitchTiming {
        double gridPpq = 4.0;
        double stampPpq = 0.0;
        bool hasStamp = false;
        bool resolved = false;
        double pointPpq = 0.0;
    };

    PatternView pattern_; // what is playing
    std::array<SlotEntry, kSlotCount> slots_{};
    int selected_ = 0;                      // slot parameter, 0-based
    int playingSlot_ = 0;                   // slot that `pattern_` came from; -1 when its entry was replaced meanwhile
    OwnedPattern* detachedOwner_ = nullptr; // owner of the playing pattern after its entry was replaced
    double slotGridPpq_ = 4.0;
    PatternHandover* handover_ = nullptr;
    SwitchTiming timing_;
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
