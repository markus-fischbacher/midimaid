# mm_platform: the only place with operating system specific code (CLAUDE.md): the keychain for API keys (SPEC 8.5).
if(APPLE)
    add_library(mm_platform STATIC SecretStore.cpp KeychainSecretStore.cpp)
    target_link_libraries(mm_platform PRIVATE "-framework Security" "-framework CoreFoundation")
else()
    # Windows (Credential Manager) and Linux (libsecret) come later; until then the key lives in memory for the session.
    add_library(mm_platform STATIC SecretStore.cpp SystemSecretStoreFallback.cpp)
endif()
target_include_directories(mm_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(mm_platform PRIVATE mm_warnings)
