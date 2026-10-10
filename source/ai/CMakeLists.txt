# mm_ai: the AI layer without network and without JUCE (SPEC 7): provider interface, response schema, parser, mock.
add_library(mm_ai STATIC AiSchema.cpp MockProvider.cpp PatternCompact.cpp PromptBuilder.cpp)
target_include_directories(mm_ai PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_ai PUBLIC mm_core PRIVATE mm_warnings)
