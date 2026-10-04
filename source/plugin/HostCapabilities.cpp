#include "plugin/HostCapabilities.h"

namespace mm::plugin {

namespace {

bool isTargetHost(HostKind host) {
    return host == HostKind::AbletonLive || host == HostKind::Logic;
}

bool isPluginFormat(PluginFormat format) {
    return format == PluginFormat::Vst3 || format == PluginFormat::Au;
}

} // namespace

HostCapabilities deriveCapabilities(PluginFormat format, HostKind host, bool isMidiEffect) {
    HostCapabilities caps;
    caps.format = format;
    caps.host = host;

    if (format == PluginFormat::Standalone) {
        // Development app: single instance, no host.
        caps.midiOutput = true;
        caps.dragDropOut = true;
        caps.keyForwarding = true;
        return caps;
    }

    if (!isPluginFormat(format)) {
        return caps; // CLAP, LV2, AAX and unknown formats: nothing verified yet
    }

    switch (host) {
    case HostKind::AbletonLive:
        caps.midiOutput = true; // instrument variant, routed via "MIDI From"
        break;
    case HostKind::Logic:
        // Logic only passes MIDI from MIDI-FX plugins (aumi) on, not from instruments.
        caps.midiOutput = isMidiEffect && format == PluginFormat::Au;
        caps.noteEffectSlot = caps.midiOutput;
        break;
    case HostKind::Reaper:
    case HostKind::Bitwig:
        caps.midiOutput = format == PluginFormat::Vst3 && !isMidiEffect;
        break;
    case HostKind::Unknown:
        break;
    }

    // Instances share one process only in the target hosts; Bitwig may sandbox, others are unverified.
    caps.sameProcessCoupling = isTargetHost(host);
    caps.dragDropOut = isTargetHost(host);
    // keyForwarding stays false until measured in Phase 0 (ROADMAP: shortcuts planning for v1.1).
    return caps;
}

} // namespace mm::plugin
