#pragma once

#include "core/RollLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace mm::plugin {

/// Read-only roll of the voice this instance plays (SPEC 8.1 voice UI): bar lines and one rectangle per note. Editing
/// belongs to the hub and the piano roll of phase 2. The texts are fixed English until the translation table exists.
class RollView : public juce::Component {
public:
    static constexpr int kTitleHeight = 20; ///< the strip with the title above the notes

    /// `title` is drawn in the corner ("Slot 3 - Bass"), `empty` shows that the slot holds no pattern yet.
    void setContent(mm::core::RollLayout layout, juce::String title, bool empty);
    /// A muted voice is drawn dim.
    void setDimmed(bool dimmed);
    void setAccent(juce::Colour accent);
    /// The tick of the playhead inside the pattern (`mm::core::playheadTick`), none while nothing plays here. Repaints
    /// only when the line moves.
    void setPlayheadTick(std::optional<double> tick);
    std::optional<double> playheadTick() const { return playheadTick_; }

    void paint(juce::Graphics& g) override;

    const mm::core::RollLayout& layout() const { return layout_; }

protected:
    mm::core::RollLayout layout_;
    juce::String title_;
    bool empty_ = true;
    bool dimmed_ = false;
    juce::Colour accent_{0xff4fc3a1};
    std::optional<double> playheadTick_;
};

} // namespace mm::plugin
