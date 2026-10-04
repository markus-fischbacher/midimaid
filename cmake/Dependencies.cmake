# Third-party dependencies via CPM, versions pinned. Changing a version needs a decision (docs/DECISIONS.md).
if(NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
    if(DEFINED ENV{HOME})
        set(_mm_home "$ENV{HOME}")
    else()
        set(_mm_home "$ENV{USERPROFILE}") # Windows
    endif()
    set(CPM_SOURCE_CACHE "${_mm_home}/.cache/CPM" CACHE PATH "CPM source cache")
endif()

include(CPM)

# JUCE 8 (pinned, D-10). Plugin targets are added in later roadmap tasks.
CPMAddPackage(
    NAME JUCE
    GITHUB_REPOSITORY juce-framework/JUCE
    GIT_TAG 8.0.15
)

CPMAddPackage(
    NAME Catch2
    GITHUB_REPOSITORY catchorg/Catch2
    VERSION 3.16.0
    GIT_TAG v3.16.0
)

CPMAddPackage(
    NAME nlohmann_json
    GITHUB_REPOSITORY nlohmann/json
    VERSION 3.12.0
    GIT_TAG v3.12.0
)
