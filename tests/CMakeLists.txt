add_executable(mm_core_tests test_version.cpp test_midi_file.cpp test_random.cpp test_theory.cpp test_pattern.cpp test_constraints.cpp test_output_stage.cpp test_register.cpp test_chord_symbol.cpp test_style_profile.cpp test_velocity_contour.cpp test_voicing.cpp test_motif.cpp test_quality.cpp test_copy_protection.cpp test_archetype.cpp test_archetype_melody.cpp test_macros.cpp test_groove.cpp test_progression.cpp test_pattern_generator.cpp test_form_plan.cpp test_series.cpp test_golden.cpp test_slot_bank.cpp test_parameter_register.cpp test_instance_core.cpp test_group_registry.cpp test_translation.cpp test_pattern_edit.cpp test_undo_stack.cpp test_voice_lock.cpp test_variation.cpp test_variation_structural.cpp test_roll_edit.cpp test_global_settings.cpp test_ai_settings.cpp test_midi_import.cpp)
target_link_libraries(mm_core_tests PRIVATE mm_core Catch2::Catch2WithMain mm_warnings)
target_compile_definitions(mm_core_tests PRIVATE MIDIMAID_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources"
                                                  MIDIMAID_GOLDEN_DIR="${PROJECT_SOURCE_DIR}/tests/golden")

find_package(Threads REQUIRED)
add_executable(mm_ai_tests test_ai_schema.cpp test_ai_mock.cpp test_ai_prompt.cpp test_ai_compact.cpp test_ai_generation.cpp test_ai_http.cpp test_ai_models.cpp test_ai_refine.cpp test_ai_phrase_shaping.cpp)
target_link_libraries(mm_ai_tests PRIVATE mm_ai Threads::Threads Catch2::Catch2WithMain mm_warnings)
target_compile_definitions(mm_ai_tests PRIVATE MIDIMAID_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources")

add_executable(mm_mmgen_tests test_mmgen.cpp)
target_link_libraries(mm_mmgen_tests PRIVATE mm_mmgen Catch2::Catch2WithMain mm_warnings)
target_compile_definitions(mm_mmgen_tests PRIVATE MIDIMAID_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources")

add_executable(mm_plugin_tests test_host_capabilities.cpp)
target_link_libraries(mm_plugin_tests PRIVATE mm_host_capabilities Catch2::Catch2WithMain mm_warnings)

add_executable(mm_engine_tests test_pattern_player.cpp test_slot_publisher.cpp test_group_sync.cpp)
target_link_libraries(mm_engine_tests PRIVATE mm_engine Threads::Threads Catch2::Catch2WithMain mm_warnings)
target_compile_definitions(mm_engine_tests PRIVATE MIDIMAID_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources")

add_executable(mm_plugin_glue_tests test_processor_glue.cpp test_generation.cpp test_group_wiring.cpp test_ai_plugin.cpp test_ai_connection.cpp test_settings_dialog.cpp test_midi_import_plugin.cpp glue_listener.cpp)
mm_warn_sources(test_processor_glue.cpp test_generation.cpp test_group_wiring.cpp test_ai_plugin.cpp test_ai_connection.cpp test_settings_dialog.cpp test_midi_import_plugin.cpp glue_listener.cpp)
# The generation tests pump the message loop (runDispatchLoopUntil needs modal loops).
target_compile_definitions(mm_plugin_glue_tests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(mm_plugin_glue_tests PRIVATE mm_plugin_common Catch2::Catch2WithMain)

add_executable(mm_platform_secret_tests test_secret_store.cpp)
target_link_libraries(mm_platform_secret_tests PRIVATE mm_platform Threads::Threads Catch2::Catch2WithMain mm_warnings)

add_executable(mm_net_tests test_http_client.cpp)
mm_warn_sources(test_http_client.cpp)
target_link_libraries(mm_net_tests PRIVATE mm_plugin_common Threads::Threads Catch2::Catch2WithMain)
target_compile_definitions(mm_net_tests PRIVATE MIDIMAID_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources")

# Practice test with a real provider (ROADMAP P3-G): built with the tests, but hidden from ctest ([.live], needs a backend).
add_executable(mm_live_tests test_ai_live.cpp)
mm_warn_sources(test_ai_live.cpp)
target_link_libraries(mm_live_tests PRIVATE mm_plugin_common Threads::Threads Catch2::Catch2WithMain)
target_compile_definitions(mm_live_tests PRIVATE MIDIMAID_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources")

list(APPEND CMAKE_MODULE_PATH "${Catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(mm_core_tests)
catch_discover_tests(mm_ai_tests)
catch_discover_tests(mm_net_tests)
catch_discover_tests(mm_platform_secret_tests)
catch_discover_tests(mm_mmgen_tests)
catch_discover_tests(mm_plugin_tests)
catch_discover_tests(mm_engine_tests)
catch_discover_tests(mm_plugin_glue_tests)

add_test(
    NAME mmgen_smoke
    COMMAND mmgen series --style peak_time --archetype gallop --seeds 1 --energies 50 --bars 1 --out
            ${CMAKE_CURRENT_BINARY_DIR}/mmgen_smoke)

add_test(
    NAME cmake_format_checks
    COMMAND ${CMAKE_COMMAND} -DCMAKE_MODULE_PATH=${PROJECT_SOURCE_DIR}/cmake -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/test_formats.cmake)
