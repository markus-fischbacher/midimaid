#include "plugin/ProcessorBase.h"

// Plugin entry point of the instrument variant (VST3, AU aumu, Standalone).
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new mm::plugin::InstrumentProcessor(JucePlugin_Name);
}
