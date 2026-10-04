# mm_engine: real-time playback logic. Standard library only (no JUCE), no allocation on the audio thread.
add_library(mm_engine STATIC PatternPlayer.cpp)
target_include_directories(mm_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_engine PRIVATE mm_warnings)
