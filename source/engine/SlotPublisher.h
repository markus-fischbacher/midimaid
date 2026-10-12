#pragma once

#include "core/SlotBank.h"
#include "core/GrooveLibrary.h"
#include "core/StyleLibrary.h"
#include "engine/PatternHandover.h"

#include <array>
#include <cstdint>
#include <optional>

namespace mm::engine {

/// Message-thread side of the slots (SPEC 3.10, 6.3): renders what changed in a `SlotBank` for the voice this
/// instance plays and hands it to the engine. JUCE-free; not thread-safe (the caller serialises).
///
/// A slot is published when its revision differs from the one last published; a bank of another origin (a state
/// load, the adopted slots of a hub) counts as changed everywhere, because its revisions mean something else.
/// A slot that was published and is now empty gets a silent pattern, so the engine does not keep playing the old
/// one. A slot that was never published stays untouched (silent). Results are quantized to `gridPpq`.
class SlotPublisher {
public:
    SlotPublisher(PatternHandover& handover, const core::StyleLibrary& styles, const core::GrooveLibrary* grooves = nullptr)
        : handover_(handover), styles_(styles), grooves_(grooves) {}

    /// The voice to play, 1 to `kMaxVoices`. A change republishes every slot at the next `sync`.
    void setOutputVoice(int voice);
    int outputVoice() const { return outputVoice_; }

    /// The octave this instance adds to its voice, -2 to +2 (D-147). A change republishes every slot at the next
    /// `sync`.
    void setOctave(int octave);
    int octave() const { return octave_; }

    /// Publishes every slot that changed since the last call. Returns the number of slots published. With `stampPpq`
    /// the changes take effect at that PPQ position (planned by the hub, D-144); a stamp already behind falls back
    /// to the next grid point.
    size_t sync(const core::SlotBank& bank, double gridPpq = 4.0, std::optional<double> stampPpq = std::nullopt);
    /// Publishes one slot as an edit (no quantization; replaces the pattern that plays). False when the slot is empty.
    bool publishEdit(const core::SlotBank& bank, size_t index);
    /// Publishes `pattern` as a result of slot `index` (the Evolve layer, SPEC 6.7): it plays at the next multiple of
    /// `gridPpq` like a result, but the bank is not touched and the slot keeps counting as published with the revision
    /// it has, so a later `sync` does not replace the stage.
    /// With `stampPpq` (planned by the hub, D-144) it takes effect at that position; a stamp already behind falls back
    /// to the next grid point.
    void publishPattern(size_t index, const core::Pattern& pattern, double gridPpq,
                        std::optional<double> stampPpq = std::nullopt);
    /// Publishes the slot content of the bank again as a result (the Evolve layer ended): the unchanged slot plays
    /// from the next multiple of `gridPpq`. False when the slot is empty.
    bool restoreSlot(const core::SlotBank& bank, size_t index, double gridPpq = 4.0,
                     std::optional<double> stampPpq = std::nullopt);
    /// Forgets which revisions were published, so the next `sync` publishes every slot that has content (and every
    /// slot that was published before) again.
    void reset();

    /// Problems found while publishing (unknown style id), for the log and the tests.
    size_t styleFallbacks() const { return styleFallbacks_; }

private:
    std::unique_ptr<OwnedPattern> render(const core::Slot& slot);

    PatternHandover& handover_;
    const core::StyleLibrary& styles_;
    const core::GrooveLibrary* grooves_ = nullptr; ///< the groove templates a track may name (SPEC 3.9)
    int outputVoice_ = 1;
    int octave_ = 0;
    uint64_t origin_ = 0;                                         ///< origin of the bank that was last synced
    std::array<std::optional<uint64_t>, kSlotCount> published_{}; ///< revision last published per slot
    std::array<bool, kSlotCount> everPublished_{};                ///< survives `reset`: the engine holds something
    size_t styleFallbacks_ = 0;
};

} // namespace mm::engine
