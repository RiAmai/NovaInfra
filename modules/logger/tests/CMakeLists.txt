include(FetchContent)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

find_package(Threads REQUIRED)
include(GoogleTest)

add_executable(novainfra_logger_tests
    async/async_logger_tests.cpp
    core/logger_tests.cpp
    core/registry_tests.cpp
    formatters/pattern_formatter_tests.cpp
    sinks/callback_sink_tests.cpp
    sinks/console_sink_tests.cpp
    sinks/file_sink_tests.cpp
    sinks/rotating_file_sink_tests.cpp
)
target_link_libraries(novainfra_logger_tests
    PRIVATE
        NovaInfra::Logger
        GTest::gtest_main
        Threads::Threads
)

gtest_discover_tests(novainfra_logger_tests DISCOVERY_MODE PRE_TEST PROPERTIES TIMEOUT 30)

add_executable(novainfra_logger_backtrace_tests core/backtrace_tests.cpp)
target_link_libraries(novainfra_logger_backtrace_tests
    PRIVATE
        NovaInfra::Logger
        GTest::gtest_main
)
gtest_discover_tests(novainfra_logger_backtrace_tests DISCOVERY_MODE PRE_TEST)

add_executable(novainfra_logger_registry_header_compile_test compile/registry_header_compile_test.cpp)
target_link_libraries(novainfra_logger_registry_header_compile_test PRIVATE NovaInfra::Logger)
add_test(NAME LoggerRegistry.HeaderIsSelfContained COMMAND novainfra_logger_registry_header_compile_test)

add_executable(novainfra_logger_factory_header_compile_test compile/factory_header_compile_test.cpp)
target_link_libraries(novainfra_logger_factory_header_compile_test PRIVATE NovaInfra::Logger)
add_test(NAME LoggerFactory.HeaderIsSelfContained COMMAND novainfra_logger_factory_header_compile_test)

add_executable(novainfra_logger_async_factory_header_compile_test compile/async_factory_header_compile_test.cpp)
target_link_libraries(novainfra_logger_async_factory_header_compile_test PRIVATE NovaInfra::Logger)
add_test(NAME LoggerAsyncFactory.HeaderIsSelfContained COMMAND novainfra_logger_async_factory_header_compile_test)

add_executable(novainfra_logger_sink_factory_compile_test compile/sink_factory_compile_test.cpp)
target_link_libraries(novainfra_logger_sink_factory_compile_test PRIVATE NovaInfra::Logger)
add_test(NAME LoggerSinkFactories.RequiredArgumentsCompile COMMAND novainfra_logger_sink_factory_compile_test)

add_executable(novainfra_logger_macro_tests
    macros/macro_tests.cpp
)
target_link_libraries(novainfra_logger_macro_tests
    PRIVATE
        NovaInfra::Logger
        GTest::gtest_main
)

gtest_discover_tests(novainfra_logger_macro_tests DISCOVERY_MODE PRE_TEST)

add_executable(novainfra_logger_macro_pruned_tests
    macros/macro_pruned_tests.cpp
)
target_compile_definitions(novainfra_logger_macro_pruned_tests
    PRIVATE NOVAINFRA_LOG_ACTIVE_LEVEL=NOVAINFRA_LOG_LEVEL_WARN
)
target_link_libraries(novainfra_logger_macro_pruned_tests
    PRIVATE
        NovaInfra::Logger
        GTest::gtest_main
)
gtest_discover_tests(novainfra_logger_macro_pruned_tests DISCOVERY_MODE PRE_TEST)

add_executable(novainfra_logger_macro_no_source_tests
    macros/macro_no_source_tests.cpp
)
target_compile_definitions(novainfra_logger_macro_no_source_tests
    PRIVATE NOVAINFRA_LOG_NO_SOURCE_LOC
)
target_link_libraries(novainfra_logger_macro_no_source_tests
    PRIVATE
        NovaInfra::Logger
        GTest::gtest_main
)
gtest_discover_tests(novainfra_logger_macro_no_source_tests DISCOVERY_MODE PRE_TEST)

if(UNIX AND EXISTS "/dev/full")
    add_executable(novainfra_logger_console_failure_test
        sinks/console_write_failure_test.cpp
    )
    target_link_libraries(novainfra_logger_console_failure_test
        PRIVATE
            NovaInfra::Logger
            Threads::Threads
    )
    add_test(
        NAME LoggerConsole.WriteFailureIsReported
        COMMAND novainfra_logger_console_failure_test
    )
endif()

foreach(LOGGER_HEADER IN LISTS LOGGER_PUBLIC_HEADERS)
    string(REGEX REPLACE "^include/" "" LOGGER_INCLUDE "${LOGGER_HEADER}")
    string(REPLACE "/" "_" LOGGER_HEADER_TARGET "${LOGGER_INCLUDE}")
    string(REPLACE "." "_" LOGGER_HEADER_TARGET "${LOGGER_HEADER_TARGET}")
    set(LOGGER_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/header_compile/${LOGGER_HEADER_TARGET}.cpp")
    file(GENERATE OUTPUT "${LOGGER_HEADER_SOURCE}"
        CONTENT "#include <${LOGGER_INCLUDE}>\nint main() { return 0; }\n")

    set(LOGGER_TARGET "novainfra_logger_header_${LOGGER_HEADER_TARGET}")
    add_executable(${LOGGER_TARGET} "${LOGGER_HEADER_SOURCE}")
    target_link_libraries(${LOGGER_TARGET} PRIVATE NovaInfra::Logger)
    add_test(NAME "LoggerHeader.${LOGGER_INCLUDE}" COMMAND ${LOGGER_TARGET})
endforeach()
