#pragma once

#include "core/Archetype.h"
#include "core/Pattern.h"
#include "core/StyleProfile.h"

#include <cstdint>
#include <deque>
#include <optional>

namespace mm::core {

/// The settings of the Evolve layer (SPEC 3.13, 6.7), kept in the instance settings.
struct EvolveSettings {
    int everyPasses = 4;       ///< a new stage after this many passes of the pattern (1 to 16)
    bool fromOriginal = false; ///< every stage varies the slot itself, not the stage before
    int strengthPct = 20;      ///< 1 to 100: the strength of the variation that makes a stage
    bool operator==(const EvolveSettings&) const = default;
};

constexpr int kEvolveMinPasses = 1;
constexpr int kEvolveMaxPasses = 16;
constexpr int kDefaultEvolvePasses = 4;
constexpr int kDefaultEvolveStrength = 20;
/// How many earlier stages stay available for "back" (SPEC 6.7).
constexpr size_t kEvolveBackStages = 8;

/// Brings every field into its valid range.
EvolveSettings clampEvolveSettings(EvolveSettings settings);

/// The Evolve layer of one slot (SPEC 6.7, v1.1): a copy of the slot pattern that develops stage by stage while the
/// slot itself stays as it is. Pure data and algorithm (no JUCE, no threads); the plug-in decides when a stage is due
/// and plays the current one. Stage 0 is the copy of the slot; every `advance` makes the next stage by a variation
/// (`applyVariation`, so locked voices and dimensions stay), either of the current stage (development) or of the slot
/// (`fromOriginal`). The last `kEvolveBackStages` stages stay reachable with `back`.
class EvolveLayer {
public:
    /// Starts on `slotIndex` (0-based) with a copy of `base` as stage 0; forgets the earlier stages.
    void start(size_t slotIndex, Pattern base, uint64_t seed);
    void stop();
    bool active() const { return slot_.has_value(); }
    /// The slot the layer belongs to (0-based); only while active.
    size_t slot() const { return *slot_; }
    /// The stage that sounds now. Only while active.
    const Pattern& current() const { return stages_.back(); }
    /// The pattern the layer started from (the slot as it was). Only while active.
    const Pattern& base() const { return base_; }
    /// Number of the current stage (0 = the copy of the slot); it keeps counting when the layer goes back.
    uint32_t stage() const { return stageNumber_; }
    /// True while the current stage differs from the slot copy it started from.
    bool changed() const { return active() && current() != base_; }
    bool canBack() const { return stages_.size() > 1; }

    /// Makes the next stage. `slotPattern` is what `fromOriginal` varies (the slot as it is now). Returns false when
    /// nothing could change (everything locked, strength 0): the current stage stays and the layer counts no stage.
    bool advance(const Pattern& slotPattern, const StyleProfile& style, const ArchetypeSettings& archetype,
                 const EvolveSettings& settings);
    /// Goes back to the stage before (one of the last `kEvolveBackStages`); false at the end of the memory.
    bool back();

private:
    std::optional<size_t> slot_;
    Pattern base_;
    std::deque<Pattern> stages_; ///< oldest first, the current stage last
    uint64_t seed_ = 0;
    uint32_t stageNumber_ = 0;
};

} // namespace mm::core
