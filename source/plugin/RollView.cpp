#include "plugin/RollView.h"

#include "core/Pattern.h"
#include "core/TextKeys.h"
#include "plugin/EmbeddedTranslation.h"

#include <algorithm>
#include <cmath>

namespace mm::plugin {

void RollView::setContent(mm::core::RollLayout layout, juce::String title, bool empty) {
    layout_ = std::move(layout);
    title_ = std::move(title);
    empty_ = empty;
    const uint32_t length = std::max<uint32_t>(layout_.bars, 1) * mm::core::kTicksPerBar;
    if (length != patternTicks_) { // another length: the whole pattern again
        patternTicks_ = length;
        view_.startTick = 0;
        view_.spanTicks = length;
    }
    view_ = mm::core::clampViewport(view_, patternTicks_);
    repaint();
}

void RollView::setTimeView(uint32_t startTick, uint32_t spanTicks) {
    auto next = view_;
    next.startTick = startTick;
    next.spanTicks = spanTicks;
    next = mm::core::clampViewport(next, patternTicks_);
    if (next != view_) {
        view_ = next;
        repaint();
    }
}

void RollView::zoomAt(double x, double factor) {
    const auto width = std::max(1.0, static_cast<double>(getWidth()));
    const double share = std::clamp(x / width, 0.0, 1.0);
    const auto anchor = static_cast<uint32_t>(view_.startTick + share * view_.spanTicks);
    const auto before = view_;
    view_ = mm::core::zoomViewport(view_, factor, anchor, patternTicks_);
    repaint();
    if (onTimeViewChanged && (before.startTick != view_.startTick || before.spanTicks != view_.spanTicks)) {
        onTimeViewChanged();
    }
}

void RollView::scrollBy(int64_t deltaTicks) {
    const auto before = view_;
    view_ = mm::core::scrollViewport(view_, deltaTicks, 0, patternTicks_);
    repaint();
    if (onTimeViewChanged && before.startTick != view_.startTick) {
        onTimeViewChanged();
    }
}

void RollView::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    if (event.mods.isCommandDown()) {
        zoomAt(static_cast<double>(event.position.x), std::exp(-static_cast<double>(wheel.deltaY) * 2.0));
        return;
    }
    if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        const double amount = event.mods.isShiftDown() ? wheel.deltaY : -wheel.deltaX;
        const double ticksPerPixel = static_cast<double>(view_.spanTicks) / std::max(1.0, static_cast<double>(getWidth()));
        scrollBy(static_cast<int64_t>(std::llround(-amount * 300.0 * ticksPerPixel)));
    }
}

void RollView::setDimmed(bool dimmed) {
    if (dimmed_ != dimmed) {
        dimmed_ = dimmed;
        repaint();
    }
}

void RollView::setAccent(juce::Colour accent) {
    accent_ = accent;
    repaint();
}

void RollView::setPlayheadTick(std::optional<double> tick) {
    const bool moved = tick.has_value() != playheadTick_.has_value() ||
                       (tick && std::abs(*tick - *playheadTick_) >= 1.0);
    if (moved) {
        playheadTick_ = tick;
        repaint();
    }
}

void RollView::paint(juce::Graphics& g) {
    const auto frame = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff1f2128));
    g.fillRoundedRectangle(frame, 7.0f);
    const auto bounds = frame.withTrimmedTop(kTitleHeight); // the notes lie below the title, never under it

    {
        juce::Graphics::ScopedSaveState state(g);
        g.reduceClipRegion(bounds.toNearestInt());
        // The visible ticks map to the full width; ticks outside are cut off by the clip.
        const float width = bounds.getWidth();
        const double span = static_cast<double>(std::max<uint32_t>(view_.spanTicks, 1));
        const auto tickToX = [&](double tick) {
            return bounds.getX() + static_cast<float>((tick - view_.startTick) / span) * width;
        };

        // Bar lines, with a fainter line on every quarter note.
        for (uint32_t bar = 0; bar < layout_.bars; ++bar) {
            const double barTick = static_cast<double>(bar) * mm::core::kTicksPerBar;
            if (barTick + mm::core::kTicksPerBar < view_.startTick || barTick > view_.startTick + span) {
                continue;
            }
            g.setColour(juce::Colours::white.withAlpha(0.18f));
            g.drawVerticalLine(juce::roundToInt(tickToX(barTick)), bounds.getY(), bounds.getBottom());
            for (int beat = 1; beat < 4; ++beat) {
                g.setColour(juce::Colours::white.withAlpha(0.06f));
                g.drawVerticalLine(juce::roundToInt(tickToX(barTick + beat * (mm::core::kTicksPerBar / 4.0))),
                                   bounds.getY(), bounds.getBottom());
            }
        }

        const float alpha = dimmed_ ? 0.25f : 1.0f;
        const double patternTicks = static_cast<double>(patternTicks_);
        for (const auto& note : layout_.notes) {
            const float velocity = 0.45f + 0.55f * static_cast<float>(note.velocity) / 127.0f;
            auto rect = juce::Rectangle<float>(
                tickToX(note.x * patternTicks), bounds.getY() + note.y * bounds.getHeight(),
                std::max(static_cast<float>(note.width * patternTicks / span) * width, 2.0f),
                std::max(note.height * bounds.getHeight() - 1.0f, 2.0f));
            g.setColour(accent_.withAlpha(alpha * velocity));
            g.fillRoundedRectangle(rect, 2.0f);
        }

        if (playheadTick_ && !empty_) {
            const float x = tickToX(*playheadTick_);
            g.setColour(juce::Colours::white.withAlpha(alpha * 0.9f));
            g.drawLine(x, bounds.getY() + 1.0f, x, bounds.getBottom() - 1.0f, 1.5f);
        }
    }

    g.setColour(juce::Colour(0xff9ba1af));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(empty_ ? tr(mm::core::text::kRollEmpty) : title_, getLocalBounds().reduced(8, 4),
               juce::Justification::topLeft);

    g.setColour(juce::Colour(0xff3a3e4a));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, 1.0f);
}

} // namespace mm::plugin
