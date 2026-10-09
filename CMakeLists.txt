cmake_minimum_required(VERSION 3.25)

# macOS builds arm64 only by default; the universal binary (arm64 + x86_64) is built on explicit request.
option(MIDIMAID_UNIVERSAL "Build a universal binary on macOS (arm64;x86_64)" OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
    if(MIDIMAID_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures" FORCE)
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures" FORCE)
    endif()
endif()

project(MidiMaid VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Static libraries end up inside plugin shared objects (VST3 on Linux needs position-independent code).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(Warnings)
include(Formats)
include(Dependencies)

# AddressSanitizer and UndefinedBehaviorSanitizer for our own code (not for the third-party dependencies above).
# Findings abort the program, so a failing check fails the test.
option(MIDIMAID_SANITIZE "Build our targets with AddressSanitizer and UndefinedBehaviorSanitizer (not MSVC)" OFF)
if(MIDIMAID_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "MIDIMAID_SANITIZE is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()

add_subdirectory(source/core)
add_subdirectory(source/ai)
add_subdirectory(source/engine)
add_subdirectory(source/plugin)
add_subdirectory(tools/mmgen)
add_subdirectory(tests)
