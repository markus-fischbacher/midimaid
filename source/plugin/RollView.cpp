#include "plugin/RollView.h"

namespace mm::plugin {

void RollView::setContent(mm::core::RollLayout layout, juce::String title, bool empty) {
    layout_ = std::move(layout);
    title_ = std::move(title);
    empty_ = empty;
    repaint();
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

void RollView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff25252b));
    g.fillRoundedRectangle(bounds, 6.0f);

    // Bar lines, with a fainter line on every quarter note.
    const float width = bounds.getWidth();
    for (uint32_t bar = 0; bar < layout_.bars; ++bar) {
        const float x = bounds.getX() + width * static_cast<float>(bar) / static_cast<float>(layout_.bars);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawVerticalLine(juce::roundToInt(x), bounds.getY(), bounds.getBottom());
        for (int beat = 1; beat < 4; ++beat) {
            const float bx = x + width / static_cast<float>(layout_.bars) * static_cast<float>(beat) / 4.0f;
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawVerticalLine(juce::roundToInt(bx), bounds.getY(), bounds.getBottom());
        }
    }

    const float alpha = dimmed_ ? 0.25f : 1.0f;
    for (const auto& note : layout_.notes) {
        const float velocity = 0.45f + 0.55f * static_cast<float>(note.velocity) / 127.0f;
        auto rect = juce::Rectangle<float>(bounds.getX() + note.x * width, bounds.getY() + note.y * bounds.getHeight(),
                                           std::max(note.width * width, 2.0f),
                                           std::max(note.height * bounds.getHeight() - 1.0f, 2.0f));
        g.setColour(accent_.withAlpha(alpha * velocity));
        g.fillRoundedRectangle(rect, 2.0f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText(empty_ ? juce::String("Empty slot: the hub has not generated a pattern for it yet") : title_,
               getLocalBounds().reduced(8, 4), juce::Justification::topLeft);
}

} // namespace mm::plugin
