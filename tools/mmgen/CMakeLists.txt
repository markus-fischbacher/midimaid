# mmgen: command line tool for the listening tests of phase 1a (SPEC 12, D-79, D-124). Uses only mm_core.
add_library(mm_mmgen STATIC Mmgen.cpp)
target_include_directories(mm_mmgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mm_mmgen PUBLIC mm_core PRIVATE mm_warnings)

add_executable(mmgen main.cpp)
target_link_libraries(mmgen PRIVATE mm_mmgen mm_warnings)
target_compile_definitions(mm_mmgen PRIVATE MIDIMAID_DEFAULT_STYLES_DIR="${PROJECT_SOURCE_DIR}/resources/styles")
