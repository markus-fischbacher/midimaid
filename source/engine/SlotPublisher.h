#pragma once

#include "core/SlotBank.h"
#include "core/StyleLibrary.h"
#include "engine/PatternHandover.h"

#include <array>
#include <cstdint>
#include <optional>

namespace mm::engine {

/// Message-thread side of the slots (SPEC 3.10, 6.3): renders what changed in a `SlotBank` for the voice this
/// instance plays and hands it to the engine. JUCE-free; not thread-safe (the caller serialises).
///
/// A slot is published when its revision differs from the one last published. A slot that was published and is now
/// empty gets a silent pattern, so the engine does not keep playing the old one. A slot that was never published
/// stays untouched (the engine's built-in pattern keeps playing there). Results are quantized to `gridPpq`.
class SlotPublisher {
public:
    SlotPublisher(PatternHandover& handover, const core::StyleLibrary& styles) : handover_(handover), styles_(styles) {}

    /// The voice to play, 1 to `kMaxVoices`. A change republishes every slot at the next `sync`.
    void setOutputVoice(int voice);
    int outputVoice() const { return outputVoice_; }

    /// Publishes every slot that changed since the last call. Returns the number of slots published.
    size_t sync(const core::SlotBank& bank, double gridPpq = 4.0);
    /// Publishes one slot as an edit (no quantization; replaces the pattern that plays). False when the slot is empty.
    bool publishEdit(const core::SlotBank& bank, size_t index);
    /// Forgets which revisions were published, so the next `sync` publishes every slot that has content (and every
    /// slot that was published before) again.
    void reset();

    /// Problems found while publishing (unknown style id), for the log and the tests.
    size_t styleFallbacks() const { return styleFallbacks_; }

private:
    std::unique_ptr<OwnedPattern> render(const core::Slot& slot);

    PatternHandover& handover_;
    const core::StyleLibrary& styles_;
    int outputVoice_ = 1;
    std::array<std::optional<uint64_t>, kSlotCount> published_{}; ///< revision last published per slot
    std::array<bool, kSlotCount> everPublished_{};                 ///< survives `reset`: the engine holds something
    size_t styleFallbacks_ = 0;
};

} // namespace mm::engine
