#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace mm::plugin {

/// The look of the plug-in windows (SPEC 8.1): dark, flat, one accent colour, rounded surfaces with a thin outline and
/// one control height. Shared by the hub and the voice window.
class MidiMaidLookAndFeel : public juce::LookAndFeel_V4 {
public:
    static constexpr juce::uint32 kBackground = 0xff17181c;
    static constexpr juce::uint32 kSurface = 0xff23252d;
    static constexpr juce::uint32 kSurfaceHover = 0xff2d3039;
    static constexpr juce::uint32 kOutline = 0xff3a3e4a;
    static constexpr juce::uint32 kText = 0xffe8eaf0;
    static constexpr juce::uint32 kTextDim = 0xff9ba1af;
    static constexpr juce::uint32 kAccent = 0xff4fc3a1;
    static constexpr juce::uint32 kAccentMelody = 0xffe0a458;
    static constexpr float kRadius = 7.0f;

    MidiMaidLookAndFeel();

    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                              bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool down, int buttonX, int buttonY, int buttonW,
                      int buttonH, juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int height) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
};

} // namespace mm::plugin
