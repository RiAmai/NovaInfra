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
set(LOGGER_PUBLIC_HEADERS
    include/logger/async/async_log_thread_pool.h
    include/logger/async/async_logger.h
    include/logger/async/types.h
    include/logger/factories/async_factory.h
    include/logger/factories/sink_factories.h
    include/logger/factories/synchronous_factory.h
    include/logger/formatters/formatter.h
    include/logger/formatters/pattern_formatter.h
    include/logger/global.h
    include/logger/logger.h
    include/logger/macros.h
    include/logger/novalog.h
    include/logger/registry.h
    include/logger/sinks/base_sink.h
    include/logger/sinks/basic_file_sink.h
    include/logger/sinks/console_sink.h
    include/logger/sinks/file_event_handlers.h
    include/logger/sinks/rotating_file_sink.h
    include/logger/sinks/sink.h
    include/logger/types.h
)
set(LOGGER_DETAIL_HEADERS
    include/logger/details/async_log_msg.h
    include/logger/details/backtracer.h
    include/logger/details/error_handling.h
    include/logger/details/file_assistant.h
    include/logger/details/format_traits.h
    include/logger/details/level_storage.h
    include/logger/details/log_buffer.h
    include/logger/details/log_msg.h
    include/logger/details/log_msg_buffer.h
    include/logger/details/mutex.h
)
set(LOGGER_SRCS
    src/async/async_log_thread_pool.cpp
    src/async/async_logger.cpp
    src/details/file_assistant.cpp
    src/formatters/pattern_formatter.cpp
    src/logger.cpp
    src/registry.cpp
)

add_library(nova_logger ${LOGGER_SRCS} ${LOGGER_PUBLIC_HEADERS} ${LOGGER_DETAIL_HEADERS})
add_library(Nova::Logger ALIAS nova_logger)

set(NOVA_LOGGER_FMT_INSTALL_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/third_party")

target_compile_features(nova_logger PUBLIC cxx_std_17)
target_include_directories(nova_logger PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(nova_logger SYSTEM PUBLIC
    $<BUILD_INTERFACE:${fmt_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${NOVA_LOGGER_FMT_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(nova_logger PUBLIC FMT_HEADER_ONLY=1)
if(NOVA_NO_EXCEPTIONS)
    target_compile_definitions(nova_logger PUBLIC FMT_USE_EXCEPTIONS=0)
endif()
target_link_libraries(nova_logger PUBLIC Nova::Core Threads::Threads)
set_target_properties(nova_logger PROPERTIES EXPORT_NAME Logger)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(nova_logger PRIVATE NOVA_LIB_EXPORT)
else()
    target_compile_definitions(nova_logger PUBLIC NOVA_STATIC_DEFINE)
endif()

install(TARGETS nova_logger
    EXPORT NovaCoreTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    DIRECTORY "${fmt_SOURCE_DIR}/include/fmt"
    DESTINATION "${NOVA_LOGGER_FMT_INSTALL_INCLUDEDIR}"
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "fmt-c.h" EXCLUDE
        PATTERN "os.h" EXCLUDE
)
install(
    FILES "${fmt_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/NovaCore"
    RENAME "fmt-LICENSE"
)

if(NOVA_LOGGER_BUILD_EXAMPLES)
    add_executable(example_logger examples/core/logger_example.cpp)
    target_link_libraries(example_logger PRIVATE Nova::Logger)

    add_executable(example_backtrace examples/core/backtrace_example.cpp)
    target_link_libraries(example_backtrace PRIVATE Nova::Logger)

    add_executable(example_async_st_logger examples/async/async_st_logger_example.cpp)
    target_link_libraries(example_async_st_logger PRIVATE Nova::Logger)

    add_executable(example_rotating_file_sink examples/sinks/rotating_file_sink_example.cpp)
    target_link_libraries(example_rotating_file_sink PRIVATE Nova::Logger)

    add_executable(example_async_logger examples/async/async_logger_example.cpp)
    target_link_libraries(example_async_logger PRIVATE Nova::Logger)

    add_executable(example_registry examples/core/registry_example.cpp)
    target_link_libraries(example_registry PRIVATE Nova::Logger)

    add_executable(example_async_overflow examples/async/async_overflow_example.cpp)
    target_link_libraries(example_async_overflow PRIVATE Nova::Logger)

    add_executable(example_custom_sink_formatter examples/extensions/custom_sink_formatter_example.cpp)
    target_link_libraries(example_custom_sink_formatter PRIVATE Nova::Logger)

    add_executable(example_file_events examples/sinks/file_events_example.cpp)
    target_link_libraries(example_file_events PRIVATE Nova::Logger)
endif()

if(NOVA_LOGGER_BUILD_BENCHMARKS)
    add_executable(nova_logger_benchmarks benchmarks/logger_benchmarks.cpp)
    target_link_libraries(nova_logger_benchmarks PRIVATE Nova::Logger Threads::Threads)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    if(NOVA_NO_EXCEPTIONS)
        add_executable(nova_logger_no_exceptions_smoke tests/smoke/no_exceptions_smoke.cpp)
        target_link_libraries(nova_logger_no_exceptions_smoke PRIVATE Nova::Logger)
        add_test(NAME LoggerNoExceptions.Smoke COMMAND nova_logger_no_exceptions_smoke)
    else()
        add_subdirectory(tests)
    endif()
endif()
