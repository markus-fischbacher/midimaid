#include "plugin/MidiMaidLookAndFeel.h"

namespace mm::plugin {

MidiMaidLookAndFeel::MidiMaidLookAndFeel() {
    setColourScheme(juce::LookAndFeel_V4::ColourScheme(
        juce::Colour(kBackground), juce::Colour(kSurface), juce::Colour(kSurface), juce::Colour(kOutline),
        juce::Colour(kText), juce::Colour(kAccent), juce::Colours::black, juce::Colour(kAccent), juce::Colour(kText)));
    setColour(juce::TextButton::buttonColourId, juce::Colour(kSurface));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(kAccent));
    setColour(juce::TextButton::textColourOffId, juce::Colour(kText));
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(kSurface));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(kOutline));
    setColour(juce::ComboBox::textColourId, juce::Colour(kText));
    setColour(juce::ComboBox::arrowColourId, juce::Colour(kTextDim));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(kSurface));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(kAccent).withAlpha(0.85f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(kSurface));
    setColour(juce::TextEditor::outlineColourId, juce::Colour(kOutline));
    setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccent));
    setColour(juce::TextEditor::textColourId, juce::Colour(kText));
    setColour(juce::Label::textColourId, juce::Colour(kText));
    setColour(juce::Slider::trackColourId, juce::Colour(kAccent));
    setColour(juce::Slider::backgroundColourId, juce::Colour(kOutline));
    setColour(juce::Slider::thumbColourId, juce::Colour(kAccent));
    setColour(juce::ToggleButton::textColourId, juce::Colour(kText));
    setColour(juce::Slider::textBoxTextColourId, juce::Colour(kText));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(kSurface));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(kOutline));
}

void MidiMaidLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& background, bool highlighted, bool down) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = background;
    if (button.isEnabled()) {
        if (down) {
            fill = fill.darker(0.15f);
        } else if (highlighted) {
            fill = fill.brighter(0.12f);
        }
    } else {
        fill = fill.withMultipliedAlpha(0.5f);
    }
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, kRadius);
    // A button in the accent colour is a main action: no outline.
    if (background != juce::Colour(kAccent)) {
        g.setColour(juce::Colour(kOutline).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle(bounds, kRadius, 1.0f);
    }
}

juce::Font MidiMaidLookAndFeel::getTextButtonFont(juce::TextButton&, int height) {
    return juce::Font(juce::FontOptions(juce::jmin(15.0f, static_cast<float>(height) * 0.45f)));
}

void MidiMaidLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                       juce::ComboBox& box) {
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds, kRadius, 1.0f);
    // The chevron at the right edge.
    const float cx = static_cast<float>(width) - 15.0f;
    const float cy = static_cast<float>(height) * 0.5f;
    juce::Path chevron;
    chevron.startNewSubPath(cx - 4.0f, cy - 2.0f);
    chevron.lineTo(cx, cy + 2.0f);
    chevron.lineTo(cx + 4.0f, cy - 2.0f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(box.isEnabled() ? 1.0f : 0.5f));
    g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void MidiMaidLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(10, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font MidiMaidLookAndFeel::getComboBoxFont(juce::ComboBox& box) {
    return juce::Font(juce::FontOptions(juce::jmin(15.0f, static_cast<float>(box.getHeight()) * 0.45f)));
}

void MidiMaidLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) {
    const float size = juce::jmin(20.0f, static_cast<float>(button.getHeight()) - 6.0f);
    const auto box = juce::Rectangle<float>(1.0f, (static_cast<float>(button.getHeight()) - size) * 0.5f, size, size);
    const bool on = button.getToggleState();
    g.setColour(on ? juce::Colour(kAccent) : juce::Colour(highlighted ? kSurfaceHover : kSurface));
    g.fillRoundedRectangle(box, 5.0f);
    if (!on) {
        g.setColour(juce::Colour(kOutline));
        g.drawRoundedRectangle(box.reduced(0.5f), 5.0f, 1.0f);
    } else {
        juce::Path tick;
        tick.startNewSubPath(box.getX() + size * 0.26f, box.getCentreY());
        tick.lineTo(box.getX() + size * 0.44f, box.getY() + size * 0.70f);
        tick.lineTo(box.getX() + size * 0.76f, box.getY() + size * 0.30f);
        g.setColour(juce::Colours::black);
        g.strokePath(tick, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour(button.findColour(juce::ToggleButton::textColourId).withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.5f));
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawFittedText(button.getButtonText(),
                     button.getLocalBounds().withTrimmedLeft(juce::roundToInt(size) + 9).withTrimmedRight(2),
                     juce::Justification::centredLeft, 1);
}

} // namespace mm::plugin
