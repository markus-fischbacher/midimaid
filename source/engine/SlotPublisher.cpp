#include "engine/SlotPublisher.h"

#include "core/InstanceSettings.h"
#include "core/PlaybackRender.h"

namespace mm::engine {

void SlotPublisher::setOutputVoice(int voice) {
    const int clamped = core::clampOutputVoice(voice);
    if (clamped != outputVoice_) {
        outputVoice_ = clamped;
        reset();
    }
}

void SlotPublisher::reset() {
    published_.fill(std::nullopt);
}

std::unique_ptr<OwnedPattern> SlotPublisher::render(const core::Slot& slot) {
    auto owned = std::make_unique<OwnedPattern>();
    if (!slot.pattern) {
        owned->lengthTicks = core::kTicksPerBar; // silence of one bar
        return owned;
    }
    const core::Pattern& pattern = *slot.pattern;
    const core::StyleProfile* style = styles_.findOrFallback(pattern.styleId);
    if (style == nullptr || styles_.find(pattern.styleId) == nullptr) {
        ++styleFallbacks_;
    }
    if (style == nullptr) {
        owned->lengthTicks = pattern.lengthBars * core::kTicksPerBar;
        owned->version = pattern.version;
        return owned;
    }
    auto rendered = core::renderVoiceForPlayback(pattern, *style, static_cast<size_t>(outputVoice_ - 1));
    owned->notes = std::move(rendered.notes);
    owned->lengthTicks = rendered.lengthTicks;
    owned->version = pattern.version;
    return owned;
}

size_t SlotPublisher::sync(const core::SlotBank& bank, double gridPpq, std::optional<double> stampPpq) {
    if (bank.origin() != origin_) {
        if (origin_ != 0) {
            reset(); // a first bank has nothing published to forget
        }
        origin_ = bank.origin();
    }
    size_t count = 0;
    for (size_t index = 0; index < kSlotCount; ++index) {
        const core::Slot* slot = bank.slot(index);
        if (slot == nullptr) {
            continue;
        }
        const bool changed = !published_[index] || *published_[index] != slot->revision;
        // Never published and still empty: nothing to say.
        if (!changed || (!everPublished_[index] && !slot->pattern)) {
            continue;
        }
        if (stampPpq) {
            handover_.publishResultAt(index, render(*slot), *stampPpq, gridPpq);
        } else {
            handover_.publishResult(index, render(*slot), gridPpq);
        }
        published_[index] = slot->revision;
        everPublished_[index] = true;
        ++count;
    }
    handover_.collectReturned();
    return count;
}

bool SlotPublisher::publishEdit(const core::SlotBank& bank, size_t index) {
    const core::Slot* slot = bank.slot(index);
    if (slot == nullptr || !slot->pattern) {
        return false;
    }
    handover_.publishEdit(index, render(*slot));
    published_[index] = slot->revision;
    everPublished_[index] = true;
    handover_.collectReturned();
    return true;
}

} // namespace mm::engine
