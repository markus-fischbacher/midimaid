# Run with: cmake -DCMAKE_MODULE_PATH=<repo>/cmake -P test_formats.cmake
# Script mode has no project: set the policies explicitly (IN_LIST needs CMP0057, not the default before 3.31).
cmake_minimum_required(VERSION 3.25)

include(FormatChecks)

function(expect_ok error)
    if(NOT "${error}" STREQUAL "")
        message(FATAL_ERROR "expected ok, got: ${error}")
    endif()
endfunction()

function(expect_error error)
    if("${error}" STREQUAL "")
        message(FATAL_ERROR "expected an error, got none")
    endif()
endfunction()

mm_check_formats(r VST3 AU Standalone)
expect_ok("${r}")
mm_check_formats(r VST3 Standalone)
expect_ok("${r}")
mm_check_formats(r)
expect_error("${r}")
mm_check_formats(r VST3 VST2)
expect_error("${r}")
mm_check_formats(r VST3 CLAP)   # reserved, not buildable in v1.0
expect_error("${r}")

mm_check_au_codes(r Klrw Mdmi)
expect_ok("${r}")
mm_check_au_codes(r Klrw Mdmf)
expect_ok("${r}")
mm_check_au_codes(r klrw Mdmi)   # no uppercase in manufacturer
expect_error("${r}")
mm_check_au_codes(r Klrw MDmi)   # two uppercase in plugin code
expect_error("${r}")
mm_check_au_codes(r Klrw mdmi)   # none
expect_error("${r}")
mm_check_au_codes(r Klrw Mdm)    # wrong length
expect_error("${r}")

message(STATUS "format checks passed")
