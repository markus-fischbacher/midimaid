# Plugin targets. Format-specific decisions live only in cmake/Formats.cmake (MIDIMAID_FORMATS).
option(MIDIMAID_COPY_PLUGINS "Copy built plugins into the user plugin folders (needed for auval)" OFF)

# Format/host capability logic without JUCE, so it can be unit-tested (SPEC 2.4).
add_library(mm_host_capabilities STATIC HostCapabilities.cpp)
target_include_directories(mm_host_capabilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_host_capabilities PRIVATE mm_warnings)

# Code shared by both variants. Plugin-specific defines come from the juce_add_plugin targets.
add_library(mm_plugin_common STATIC ProcessorBase.cpp PlaceholderEditor.cpp HostDetection.cpp MidiExporter.cpp)
target_include_directories(mm_plugin_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    mm_plugin_common
    PUBLIC mm_core mm_engine mm_host_capabilities juce::juce_audio_processors
    PRIVATE mm_warnings juce::juce_recommended_config_flags)

set(_mm_plugin_defines JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)

# Instrument variant: VST3, AU (aumu), Standalone.
juce_add_plugin(
    MidiMaid
    COMPANY_NAME "${MIDIMAID_COMPANY_NAME}"
    PRODUCT_NAME "${MIDIMAID_PRODUCT_NAME}"
    BUNDLE_ID "${MIDIMAID_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE "${MIDIMAID_MANUFACTURER_CODE}"
    PLUGIN_CODE "${MIDIMAID_PLUGIN_CODE}"
    FORMATS ${MIDIMAID_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD ${MIDIMAID_COPY_PLUGINS})
target_sources(MidiMaid PRIVATE InstrumentEntry.cpp)
target_compile_definitions(MidiMaid PUBLIC ${_mm_plugin_defines})
target_link_libraries(
    MidiMaid
    PRIVATE mm_plugin_common mm_warnings
    PUBLIC juce::juce_audio_utils juce::juce_recommended_config_flags)

# MIDI-FX variant: AU (aumi) only.
if(MIDIMAID_FX_FORMATS)
    juce_add_plugin(
        MidiMaidFX
        COMPANY_NAME "${MIDIMAID_COMPANY_NAME}"
        PRODUCT_NAME "${MIDIMAID_FX_PRODUCT_NAME}"
        BUNDLE_ID "${MIDIMAID_FX_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE "${MIDIMAID_MANUFACTURER_CODE}"
        PLUGIN_CODE "${MIDIMAID_FX_PLUGIN_CODE}"
        FORMATS ${MIDIMAID_FX_FORMATS}
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        COPY_PLUGIN_AFTER_BUILD ${MIDIMAID_COPY_PLUGINS})
    target_sources(MidiMaidFX PRIVATE MidiFxEntry.cpp)
    target_compile_definitions(MidiMaidFX PUBLIC ${_mm_plugin_defines})
    target_link_libraries(
        MidiMaidFX
        PRIVATE mm_plugin_common mm_warnings
        PUBLIC juce::juce_audio_utils juce::juce_recommended_config_flags)
endif()
