# Warnings are errors for our own targets only (never for third-party code).
# No fast-math anywhere in our targets (SPEC 4.3).
add_library(mm_warnings INTERFACE)
if(MSVC)
    target_compile_options(mm_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(mm_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()
