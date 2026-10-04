# Warnings are errors for our own code only (never for third-party code).
# No fast-math anywhere in our targets (SPEC 4.3).
if(MSVC)
    set(MM_WARNING_FLAGS /W4 /WX)
else()
    set(MM_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)
endif()

# For targets without third-party sources.
add_library(mm_warnings INTERFACE)
target_compile_options(mm_warnings INTERFACE ${MM_WARNING_FLAGS})

# For targets that also compile third-party sources (JUCE modules are compiled into the consuming target):
# applies the flags to our own source files only. Call from the directory that creates the target.
function(mm_warn_sources)
    set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "${MM_WARNING_FLAGS}")
endfunction()
