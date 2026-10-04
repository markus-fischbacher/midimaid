#include "plugin/HostCapabilities.h"

#include <catch2/catch_test_macros.hpp>

using namespace mm::plugin;

TEST_CASE("unknown and reserved formats have no capabilities", "[host]") {
    for (auto format : {PluginFormat::Unknown, PluginFormat::Clap, PluginFormat::Lv2, PluginFormat::Aax}) {
        auto caps = deriveCapabilities(format, HostKind::AbletonLive, false);
        CHECK_FALSE(caps.midiOutput);
        CHECK_FALSE(caps.noteEffectSlot);
        CHECK_FALSE(caps.sameProcessCoupling);
        CHECK_FALSE(caps.dragDropOut);
        CHECK_FALSE(caps.keyForwarding);
    }
}

TEST_CASE("standalone runs without host and without coupling", "[host]") {
    auto caps = deriveCapabilities(PluginFormat::Standalone, HostKind::Unknown, false);
    CHECK(caps.midiOutput);
    CHECK(caps.dragDropOut);
    CHECK(caps.keyForwarding);
    CHECK_FALSE(caps.noteEffectSlot);
    CHECK_FALSE(caps.sameProcessCoupling);
}

TEST_CASE("Live: instrument variant outputs MIDI, no MIDI-FX slot", "[host]") {
    for (auto format : {PluginFormat::Vst3, PluginFormat::Au}) {
        auto caps = deriveCapabilities(format, HostKind::AbletonLive, false);
        CHECK(caps.midiOutput);
        CHECK_FALSE(caps.noteEffectSlot);
        CHECK(caps.sameProcessCoupling);
        CHECK(caps.dragDropOut);
        CHECK_FALSE(caps.keyForwarding);
    }
}

TEST_CASE("Logic: only the AU MIDI-FX variant outputs MIDI", "[host]") {
    auto fx = deriveCapabilities(PluginFormat::Au, HostKind::Logic, true);
    CHECK(fx.midiOutput);
    CHECK(fx.noteEffectSlot);
    CHECK(fx.sameProcessCoupling);

    auto instrument = deriveCapabilities(PluginFormat::Au, HostKind::Logic, false);
    CHECK_FALSE(instrument.midiOutput);
    CHECK_FALSE(instrument.noteEffectSlot);
    CHECK(instrument.sameProcessCoupling);
}

TEST_CASE("other hosts: conservative defaults", "[host]") {
    for (auto host : {HostKind::Reaper, HostKind::Bitwig}) {
        auto vst3 = deriveCapabilities(PluginFormat::Vst3, host, false);
        CHECK(vst3.midiOutput);
        CHECK_FALSE(vst3.sameProcessCoupling);
        CHECK_FALSE(vst3.dragDropOut);
        CHECK_FALSE(vst3.noteEffectSlot);
    }
    auto unknown = deriveCapabilities(PluginFormat::Vst3, HostKind::Unknown, false);
    CHECK_FALSE(unknown.midiOutput);
    CHECK_FALSE(unknown.sameProcessCoupling);
}

TEST_CASE("derived capabilities carry format and host", "[host]") {
    auto caps = deriveCapabilities(PluginFormat::Vst3, HostKind::Bitwig, false);
    CHECK(caps.format == PluginFormat::Vst3);
    CHECK(caps.host == HostKind::Bitwig);
}
