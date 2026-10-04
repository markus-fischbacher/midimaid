#include "plugin/ProcessorBase.h"

// Plugin entry point of the MIDI-FX variant (AU aumi).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new mm::plugin::MidiFxProcessor(JucePlugin_Name);
}
