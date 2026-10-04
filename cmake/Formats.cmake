# Format list and plugin identity (SPEC 2.2, 2.4, D-81). Identity values are fixed: never change them,
# existing DAW projects depend on them.
include(FormatChecks)

if(APPLE)
    set(_mm_default_formats "VST3;AU;Standalone")
else()
    set(_mm_default_formats "VST3;Standalone")
endif()
set(MIDIMAID_FORMATS "${_mm_default_formats}" CACHE STRING "Plugin formats to build (VST3;AU;Standalone)")

mm_check_formats(_mm_format_error ${MIDIMAID_FORMATS})
if(_mm_format_error)
    message(FATAL_ERROR "${_mm_format_error}")
endif()

# The MIDI-FX variant exists only as AU (aumi).
if("AU" IN_LIST MIDIMAID_FORMATS)
    set(MIDIMAID_FX_FORMATS "AU")
else()
    set(MIDIMAID_FX_FORMATS "")
endif()

set(MIDIMAID_COMPANY_NAME "Klirrwerk")
set(MIDIMAID_MANUFACTURER_CODE "Klrw")

# Instrument variant
set(MIDIMAID_PRODUCT_NAME "MidiMaid")
set(MIDIMAID_PLUGIN_CODE "Mdmi")
set(MIDIMAID_BUNDLE_ID "com.klirrwerk.midimaid")
set(MIDIMAID_CLAP_ID "com.klirrwerk.midimaid")
set(MIDIMAID_LV2_URI "https://klirrwerk.com/plugins/midimaid")

# MIDI-FX variant (AU aumi)
set(MIDIMAID_FX_PRODUCT_NAME "MidiMaid MIDI")
set(MIDIMAID_FX_PLUGIN_CODE "Mdmf")
set(MIDIMAID_FX_BUNDLE_ID "com.klirrwerk.midimaid.fx")
set(MIDIMAID_FX_CLAP_ID "com.klirrwerk.midimaid.fx")
set(MIDIMAID_FX_LV2_URI "https://klirrwerk.com/plugins/midimaid-fx")

mm_check_au_codes(_mm_au_error "${MIDIMAID_MANUFACTURER_CODE}" "${MIDIMAID_PLUGIN_CODE}")
if(_mm_au_error)
    message(FATAL_ERROR "${_mm_au_error}")
endif()
mm_check_au_codes(_mm_au_error "${MIDIMAID_MANUFACTURER_CODE}" "${MIDIMAID_FX_PLUGIN_CODE}")
if(_mm_au_error)
    message(FATAL_ERROR "${_mm_au_error}")
endif()

message(STATUS "MidiMaid formats: ${MIDIMAID_FORMATS} (MIDI-FX: ${MIDIMAID_FX_FORMATS})")
