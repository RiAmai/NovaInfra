include(FetchContent)
if(NOT TARGET GTest::gtest_main)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)
add_executable(novainfra_config_tests config_tests.cpp)
target_link_libraries(novainfra_config_tests PRIVATE NovaInfra::Config GTest::gtest_main)
gtest_discover_tests(novainfra_config_tests DISCOVERY_MODE PRE_TEST)

foreach(CONFIG_HEADER IN ITEMS config/config.h config/config_manager.h config/error.h config/schema.h config/snapshot.h config/types.h config/value.h)
    string(REPLACE "/" "_" CONFIG_HEADER_TARGET "${CONFIG_HEADER}")
    string(REPLACE "." "_" CONFIG_HEADER_TARGET "${CONFIG_HEADER_TARGET}")
    set(CONFIG_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/header_compile/${CONFIG_HEADER_TARGET}.cpp")
    file(GENERATE OUTPUT "${CONFIG_HEADER_SOURCE}"
        CONTENT "#include <${CONFIG_HEADER}>\nint main() { return 0; }\n")
    add_executable(novainfra_config_header_${CONFIG_HEADER_TARGET} "${CONFIG_HEADER_SOURCE}")
    target_link_libraries(novainfra_config_header_${CONFIG_HEADER_TARGET} PRIVATE NovaInfra::Config)
    add_test(NAME "ConfigHeader.${CONFIG_HEADER}" COMMAND novainfra_config_header_${CONFIG_HEADER_TARGET})
endforeach()
