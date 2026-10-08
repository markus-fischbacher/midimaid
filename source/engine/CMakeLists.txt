# mm_engine: real-time playback logic. Standard library only (no JUCE), no allocation on the audio thread.
add_library(mm_engine STATIC PatternPlayer.cpp PatternHandover.cpp SlotPublisher.cpp)
target_include_directories(mm_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_engine PUBLIC mm_core PRIVATE mm_warnings)
