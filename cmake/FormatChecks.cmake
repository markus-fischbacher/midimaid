# Pure validation helpers for the format list and the plugin identity (SPEC 2.2, 2.4).
# They report problems through an output variable so they can be unit-tested with `cmake -P`.

set(MIDIMAID_KNOWN_FORMATS VST3 AU Standalone CLAP LV2 AAX)
# Formats that are actually built in v1.0; the others are reserved (identity only).
set(MIDIMAID_BUILDABLE_FORMATS VST3 AU Standalone)

# mm_check_formats(<out_var> <formats...>): empty result = ok, otherwise an error message.
function(mm_check_formats out_var)
    set(error "")
    if(NOT ARGN)
        set(error "MIDIMAID_FORMATS is empty")
    endif()
    foreach(format IN LISTS ARGN)
        if(NOT format IN_LIST MIDIMAID_KNOWN_FORMATS)
            set(error "unknown format '${format}' in MIDIMAID_FORMATS (known: ${MIDIMAID_KNOWN_FORMATS})")
        elseif(NOT format IN_LIST MIDIMAID_BUILDABLE_FORMATS)
            set(error "format '${format}' is reserved but not buildable yet (buildable: ${MIDIMAID_BUILDABLE_FORMATS})")
        endif()
    endforeach()
    set(${out_var} "${error}" PARENT_SCOPE)
endfunction()

# mm_check_au_codes(<out_var> <manufacturer> <plugin>): AU rules from SPEC 2.2.
function(mm_check_au_codes out_var manufacturer plugin)
    set(error "")
    string(LENGTH "${manufacturer}" manufacturer_length)
    string(LENGTH "${plugin}" plugin_length)
    if(NOT manufacturer_length EQUAL 4 OR NOT plugin_length EQUAL 4)
        set(error "AU codes must be exactly 4 characters")
    elseif(NOT manufacturer MATCHES "[A-Z]")
        set(error "manufacturer code '${manufacturer}' needs at least one uppercase letter")
    else()
        string(REGEX REPLACE "[^A-Z]" "" plugin_upper "${plugin}")
        string(LENGTH "${plugin_upper}" plugin_upper_count)
        if(NOT plugin_upper_count EQUAL 1)
            set(error "plugin code '${plugin}' needs exactly one uppercase letter")
        endif()
    endif()
    set(${out_var} "${error}" PARENT_SCOPE)
endfunction()
