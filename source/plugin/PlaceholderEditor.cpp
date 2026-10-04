#include "plugin/PlaceholderEditor.h"

namespace mm::plugin {

PlaceholderEditor::PlaceholderEditor(juce::AudioProcessor& processor) : juce::AudioProcessorEditor(processor) {
    setSize(600, 320);
}

void PlaceholderEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff1b1b1f));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(24.0f));
    g.drawText(getAudioProcessor()->getName(), getLocalBounds(), juce::Justification::centred);
}

} // namespace mm::plugin
