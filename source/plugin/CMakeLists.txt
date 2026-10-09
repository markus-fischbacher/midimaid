# Plugin targets. Format-specific decisions live only in cmake/Formats.cmake (MIDIMAID_FORMATS).
option(MIDIMAID_COPY_PLUGINS "Copy built plugins into the user plugin folders (needed for auval)" OFF)

# Format/host capability logic without JUCE, so it can be unit-tested (SPEC 2.4).
add_library(mm_host_capabilities STATIC HostCapabilities.cpp)
target_include_directories(mm_host_capabilities PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_host_capabilities PRIVATE mm_warnings)

# JUCE modules are compiled into every consuming target, so all of them need the same JUCE configuration.
add_library(mm_juce_config INTERFACE)
target_compile_definitions(mm_juce_config INTERFACE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(mm_juce_config INTERFACE juce::juce_recommended_config_flags)

# Warnings as errors for our own files only (the targets below also compile JUCE module sources).
mm_warn_sources(
    ProcessorBase.cpp
    EmbeddedStyles.cpp
    GenerationService.cpp
    GroupLink.cpp
    GroupText.cpp
    RollView.cpp
    EditorParts.cpp
    PlaceholderEditor.cpp
    HostDetection.cpp
    MidiExporter.cpp
    InstrumentEntry.cpp
    MidiFxEntry.cpp)

# Code shared by both variants.
# The style profiles travel inside the plugin binary (D-139).
file(GLOB MIDIMAID_STYLE_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/styles/*.json)
juce_add_binary_data(mm_styles_data HEADER_NAME "MidiMaidStyles.h" NAMESPACE MidiMaidStyles SOURCES ${MIDIMAID_STYLE_FILES})
set_target_properties(mm_styles_data PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The UI texts travel inside the plugin binary, too (SPEC 8.2).
file(GLOB MIDIMAID_I18N_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/i18n/*.json)
juce_add_binary_data(mm_i18n_data HEADER_NAME "MidiMaidI18n.h" NAMESPACE MidiMaidI18n SOURCES ${MIDIMAID_I18N_FILES})
set_target_properties(mm_i18n_data PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(mm_plugin_common STATIC ProcessorBase.cpp EmbeddedStyles.cpp EmbeddedTranslation.cpp GenerationService.cpp GroupLink.cpp GroupText.cpp RollView.cpp EditorParts.cpp PlaceholderEditor.cpp HostDetection.cpp MidiExporter.cpp)
target_include_directories(mm_plugin_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(
    mm_plugin_common
    PUBLIC mm_core mm_engine mm_host_capabilities mm_juce_config juce::juce_audio_processors mm_styles_data mm_i18n_data)

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
target_link_libraries(
    MidiMaid
    PRIVATE mm_plugin_common
    PUBLIC juce::juce_audio_utils)

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
    target_link_libraries(
        MidiMaidFX
        PRIVATE mm_plugin_common
        PUBLIC juce::juce_audio_utils)
endif()
