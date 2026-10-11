#pragma once

#include "core/RollEdit.h"
#include "core/RollLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <optional>

namespace mm::plugin {

/// Read-only roll of the voice this instance plays (SPEC 8.1 voice UI): bar lines and one rectangle per note. Editing
/// belongs to the hub. It shows the whole pattern; Cmd/Ctrl and the wheel zoom in time, Shift or a sideways wheel scroll
/// (the same window logic as the roll of the hub).
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

    /// The ticks of the pattern that are visible (the whole pattern after a new pattern or length).
    uint32_t viewStart() const { return view_.startTick; }
    uint32_t viewSpan() const { return view_.spanTicks; }
    /// The length of the shown pattern in ticks (whole bars).
    uint32_t patternTicks() const { return patternTicks_; }
    void setTimeView(uint32_t startTick, uint32_t spanTicks);
    void zoomAt(double x, double factor);
    void scrollBy(int64_t deltaTicks);
    /// Called after the window changed by hand.
    std::function<void()> onTimeViewChanged;

    void paint(juce::Graphics& g) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

    const mm::core::RollLayout& layout() const { return layout_; }

protected:
    mm::core::RollLayout layout_;
    juce::String title_;
    bool empty_ = true;
    bool dimmed_ = false;
    juce::Colour accent_{0xff4fc3a1};
    std::optional<double> playheadTick_;
    mm::core::RollViewport view_;
    uint32_t patternTicks_ = mm::core::kTicksPerBar;
};

} // namespace mm::plugin
