include(FetchContent)
find_package(Threads REQUIRED)

# Download the pinned fmt headers without adding fmt's own library or install targets.
FetchContent_Declare(
    fmt
    URL https://github.com/fmtlib/fmt/archive/refs/tags/12.2.0.zip
    URL_HASH SHA256=8bc0dfbeccc1ba96dc9c0f9bf0bbc95f3c7a35308122618638c5755f389e5e72
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(fmt)

if(NOT fmt_POPULATED)
    FetchContent_Populate(fmt)
endif()

# logger
file(GLOB LOGGER_PUBLIC_HEADERS
    "include/logger/*.h"
    "include/logger/async/*.h"
    "include/logger/factories/*.h"
    "include/logger/formatters/*.h"
    "include/logger/sinks/*.h"
)
file(GLOB LOGGER_DETAIL_HEADERS "include/logger/details/*.h")
file(GLOB LOGGER_SRCS "src/*.cpp" "src/async/*.cpp" "src/details/*.cpp" "src/formatters/*.cpp")

add_library(novainfra_logger ${LOGGER_SRCS} ${LOGGER_PUBLIC_HEADERS} ${LOGGER_DETAIL_HEADERS})
add_library(NovaInfra::Logger ALIAS novainfra_logger)

set(NOVAINFRA_LOGGER_FMT_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/third_party")

target_compile_features(novainfra_logger PUBLIC cxx_std_17)
target_include_directories(novainfra_logger PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(novainfra_logger SYSTEM PUBLIC
    $<BUILD_INTERFACE:${fmt_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${NOVAINFRA_LOGGER_FMT_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(novainfra_logger PUBLIC FMT_HEADER_ONLY=1)

if(NOVAINFRA_NO_EXCEPTIONS)
    target_compile_definitions(novainfra_logger PUBLIC FMT_USE_EXCEPTIONS=0)
endif()

target_link_libraries(novainfra_logger PUBLIC NovaInfra::Core Threads::Threads)
set_target_properties(novainfra_logger PROPERTIES EXPORT_NAME Logger)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(novainfra_logger PRIVATE NOVAINFRA_LIB_EXPORT)
else()
    target_compile_definitions(novainfra_logger PUBLIC NOVAINFRA_STATIC_DEFINE)
endif()

install(TARGETS novainfra_logger
    EXPORT NovaInfraTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    DIRECTORY "${fmt_SOURCE_DIR}/include/fmt"
    DESTINATION "${NOVAINFRA_LOGGER_FMT_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
    PATTERN "*.h"
    PATTERN "fmt-c.h" EXCLUDE
    PATTERN "os.h" EXCLUDE
)
install(
    FILES "${fmt_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/NovaInfra"
    RENAME "fmt-LICENSE"
)

if(NOVAINFRA_LOGGER_BUILD_EXAMPLES)
    add_executable(example_logger examples/core/logger_example.cpp)
    target_link_libraries(example_logger PRIVATE NovaInfra::Logger)

    add_executable(example_backtrace examples/core/backtrace_example.cpp)
    target_link_libraries(example_backtrace PRIVATE NovaInfra::Logger)

    add_executable(example_async_st_logger examples/async/async_st_logger_example.cpp)
    target_link_libraries(example_async_st_logger PRIVATE NovaInfra::Logger)

    add_executable(example_rotating_file_sink examples/sinks/rotating_file_sink_example.cpp)
    target_link_libraries(example_rotating_file_sink PRIVATE NovaInfra::Logger)

    add_executable(example_async_logger examples/async/async_logger_example.cpp)
    target_link_libraries(example_async_logger PRIVATE NovaInfra::Logger)

    add_executable(example_registry examples/core/registry_example.cpp)
    target_link_libraries(example_registry PRIVATE NovaInfra::Logger)

    add_executable(example_async_overflow examples/async/async_overflow_example.cpp)
    target_link_libraries(example_async_overflow PRIVATE NovaInfra::Logger)

    add_executable(example_custom_sink_formatter examples/extensions/custom_sink_formatter_example.cpp)
    target_link_libraries(example_custom_sink_formatter PRIVATE NovaInfra::Logger)

    add_executable(example_file_events examples/sinks/file_events_example.cpp)
    target_link_libraries(example_file_events PRIVATE NovaInfra::Logger)
endif()

if(NOVAINFRA_LOGGER_BUILD_BENCHMARKS)
    add_executable(novainfra_logger_benchmarks benchmarks/logger_benchmarks.cpp)
    target_link_libraries(novainfra_logger_benchmarks PRIVATE NovaInfra::Logger Threads::Threads)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    if(NOVAINFRA_NO_EXCEPTIONS)
        add_executable(novainfra_logger_no_exceptions_smoke tests/smoke/no_exceptions_smoke.cpp)
        target_link_libraries(novainfra_logger_no_exceptions_smoke PRIVATE NovaInfra::Logger)
        add_test(NAME LoggerNoExceptions.Smoke COMMAND novainfra_logger_no_exceptions_smoke)
    else()
        add_subdirectory(tests)
    endif()
endif()
