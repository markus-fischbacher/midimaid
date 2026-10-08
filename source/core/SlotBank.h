#pragma once

#include "core/Pattern.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm::core {

/// Slots per instance (SPEC 3.10). The engine has its own constant of the same value.
constexpr size_t kSlotCount = 16;
/// Results kept per slot in memory (SPEC 3.6).
constexpr size_t kMaxHistory = 20;
/// Results kept per slot in the project state, besides the current pattern (SPEC 9.1, D-137).
constexpr size_t kStateHistory = 5;
/// Longest slot name in bytes (UTF-8).
constexpr size_t kMaxNameBytes = 32;
/// Colour marks: 0 = none, 1 to 8 index a palette that the UI defines.
constexpr uint8_t kMaxColor = 8;

/// Cleans a slot name: control characters removed, spaces at both ends trimmed, cut to `kMaxNameBytes` at a UTF-8
/// character boundary. The UI shows an empty name as "Slot n" through the translation table.
std::string sanitizeSlotName(std::string_view name);

/// What copy and paste carry: the pattern (all voices, note ids and counter unchanged, SPEC 5), name and colour.
struct SlotContent {
    std::optional<Pattern> pattern;
    std::string name;
    uint8_t color = 0;
};

/// One slot. An empty slot has no pattern.
struct Slot {
    std::optional<Pattern> pattern; ///< the current pattern (all voices)
    std::string name;
    uint8_t color = 0;
    std::vector<Pattern> history; ///< results, oldest first, at most `kMaxHistory` (SPEC 3.6)
    size_t cursor = 0;            ///< index in `history` of the entry the current pattern shows or is based on
    uint64_t revision = 0;        ///< raised by every change of this slot; consumers detect changes with it
};

/// The 16 slots of an instance on the message-thread side (SPEC 3.10). No JUCE, no audio thread: the engine gets its
/// copy through the handover (SPEC 6.3). Indices are 0-based; an invalid index makes an operation return false.
///
/// Results (generate, variation, refine, library load, paste, import) are appended to the history and become the
/// current pattern. Note edits only replace the current pattern (SPEC 3.6). Every stored pattern gets the next
/// consecutive `Pattern::version` of the bank.
class SlotBank {
public:
    const Slot* slot(size_t index) const;
    /// Identifies this bank and its copies: unique per constructed bank in the process, kept by copies. Revisions are
    /// only comparable between banks of the same origin (the slot publisher resets when the origin changes).
    uint64_t origin() const { return origin_; }
    bool isEmpty(size_t index) const;
    /// The version the next stored pattern gets.
    uint64_t nextVersion() const { return nextVersion_; }

    /// Stores `pattern` as a result. False for an invalid index or an invalid pattern (nothing changes).
    bool setResult(size_t index, Pattern pattern);
    /// Replaces the current pattern with an edited one (no history entry). False when the slot is empty.
    bool edit(size_t index, Pattern pattern);
    /// Browse the results (Cmd+[ and Cmd+]): the entry becomes the current pattern. False at the ends.
    bool historyBack(size_t index);
    bool historyForward(size_t index);

    bool setName(size_t index, std::string_view name);
    bool setColor(size_t index, uint8_t color);

    /// The content of a slot for copy; an empty slot gives content without a pattern. Nullopt for an invalid index.
    std::optional<SlotContent> copy(size_t index) const;
    /// Pastes content: the pattern counts as a result, name and colour are taken over. False without a pattern.
    bool paste(size_t index, const SlotContent& content);
    /// Resets the slot completely: pattern, history, name and colour.
    bool clear(size_t index);
    /// Exchanges two slots with their history, name and colour.
    bool swap(size_t first, size_t second);

    /// For the loader: restores a slot and the version counter without validation (the loader validates).
    void restore(size_t index, Slot slot);
    void restoreNextVersion(uint64_t next) { nextVersion_ = next; }

private:
    bool valid(size_t index) const { return index < kSlotCount; }
    bool store(size_t index, Pattern pattern, bool asResult);

    static uint64_t nextOrigin();

    std::array<Slot, kSlotCount> slots_{};
    uint64_t nextVersion_ = 1;
    uint64_t origin_ = nextOrigin();
};

} // namespace mm::core
