add_executable(mm_core_tests test_version.cpp)
target_link_libraries(mm_core_tests PRIVATE mm_core Catch2::Catch2WithMain mm_warnings)

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(mm_core_tests)

add_test(
    NAME cmake_format_checks
    COMMAND ${CMAKE_COMMAND} -DCMAKE_MODULE_PATH=${PROJECT_SOURCE_DIR}/cmake -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_formats.cmake)
