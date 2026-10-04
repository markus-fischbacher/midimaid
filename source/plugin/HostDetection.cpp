#include "plugin/HostDetection.h"

namespace mm::plugin {

namespace {

PluginFormat toFormat(juce::AudioProcessor::WrapperType type) {
    switch (type) {
    case juce::AudioProcessor::wrapperType_VST3:
        return PluginFormat::Vst3;
    case juce::AudioProcessor::wrapperType_AudioUnit:
        return PluginFormat::Au;
    case juce::AudioProcessor::wrapperType_Standalone:
        return PluginFormat::Standalone;
    case juce::AudioProcessor::wrapperType_AAX:
        return PluginFormat::Aax;
    case juce::AudioProcessor::wrapperType_LV2:
        return PluginFormat::Lv2;
    default:
        return PluginFormat::Unknown;
    }
}

HostKind toHost(const juce::PluginHostType& host) {
    if (host.isAbletonLive()) {
        return HostKind::AbletonLive;
    }
    if (host.isLogic()) {
        return HostKind::Logic;
    }
    if (host.isReaper()) {
        return HostKind::Reaper;
    }
    if (host.isBitwigStudio()) {
        return HostKind::Bitwig;
    }
    return HostKind::Unknown;
}

} // namespace

HostCapabilities detectHostCapabilities(const juce::AudioProcessor& processor) {
    return deriveCapabilities(toFormat(processor.wrapperType), toHost(juce::PluginHostType()),
                              processor.isMidiEffect());
}

} // namespace mm::plugin
