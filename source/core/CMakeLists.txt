# mm_core: music theory, pattern model, generators. Standard library only (no JUCE).
add_library(mm_core STATIC version.cpp PlaybackPattern.cpp MidiFile.cpp Random.cpp Theory.cpp Pattern.cpp PatternValidation.cpp PatternJson.cpp Constraints.cpp KickGrid.cpp OutputStage.cpp Register.cpp)
target_include_directories(mm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_core PUBLIC nlohmann_json::nlohmann_json PRIVATE mm_warnings)
