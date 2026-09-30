cmake_minimum_required(VERSION 3.21)
project(NovaInfra VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NovaInfra only supports Linux")
endif()

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(
        CMAKE_INSTALL_PREFIX
        "${PROJECT_SOURCE_DIR}/install/novainfra/"
        CACHE PATH "NovaInfra install prefix" FORCE
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(NOVAINFRA_ENABLE_IPO "Enable interprocedural optimization for Release builds" OFF)

if(NOVAINFRA_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT NOVAINFRA_IPO_SUPPORTED OUTPUT NOVAINFRA_IPO_ERROR LANGUAGES CXX)

    if(NOVAINFRA_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(FATAL_ERROR "NOVAINFRA_ENABLE_IPO is not supported: ${NOVAINFRA_IPO_ERROR}")
    endif()
endif()

# modules options
option(MODULES_LOGGER_ENABLE "Enable logger module" ON)
option(MODULES_EVENT_BUS_ENABLE "Enable event bus module" ON)
option(MODULES_CONFIG_ENABLE "Enable config module" ON)
option(MODULES_THREAD_POOL_ENABLE "Enable thread pool module" ON)
option(MODULES_SQLITE_POOL_ENABLE "Enable sqlite pool module" ON)
option(MODULES_STATE_MACHINE_ENABLE "Enable state machine module" ON)

# novainfra logger options
if(MODULES_LOGGER_ENABLE)
    option(NOVAINFRA_LOGGER_BUILD_EXAMPLES "Build logger examples" ON)
    option(NOVAINFRA_LOGGER_BUILD_BENCHMARKS "Build logger microbenchmarks" OFF)
endif()

# novainfra core options
option(NOVAINFRA_NO_EXCEPTIONS "Build NovaInfra without C++ exceptions" OFF)
option(NOVAINFRA_NO_TZ_OFFSET "Omit %z timezone offset" OFF)
option(NOVAINFRA_PREVENT_CHILD_FD "Prevent child processes from inheriting log file descriptors" OFF)
option(NOVAINFRA_NO_THREAD_ID "Disable thread-id queries on log calls" OFF)
option(NOVAINFRA_NO_TLS "Disable thread-local storage" OFF)
option(NOVAINFRA_CLOCK_COARSE "Use CLOCK_REALTIME_COARSE" OFF)

add_subdirectory(modules/core)

if(MODULES_LOGGER_ENABLE)
    add_subdirectory(modules/logger)
endif()

if(MODULES_EVENT_BUS_ENABLE)
    add_subdirectory(modules/event_bus)
endif()

if(MODULES_CONFIG_ENABLE)
    add_subdirectory(modules/config)
endif()

if(MODULES_THREAD_POOL_ENABLE)
    add_subdirectory(modules/thread_pool)
endif()

if(MODULES_SQLITE_POOL_ENABLE)
    add_subdirectory(modules/sqlite_pool)
endif()

if(MODULES_STATE_MACHINE_ENABLE)
    add_subdirectory(modules/state_machine)
endif()

foreach(NOVAINFRA_OPTION IN ITEMS
    NOVAINFRA_CLOCK_COARSE
    NOVAINFRA_NO_EXCEPTIONS
    NOVAINFRA_NO_THREAD_ID
    NOVAINFRA_NO_TLS
    NOVAINFRA_NO_TZ_OFFSET
    NOVAINFRA_PREVENT_CHILD_FD)
    if(${NOVAINFRA_OPTION})
        target_compile_definitions(novainfra_core INTERFACE ${NOVAINFRA_OPTION})
    endif()
endforeach()

if(NOVAINFRA_NO_EXCEPTIONS)
    target_compile_options(novainfra_core INTERFACE -fno-exceptions)
endif()

set(NOVAINFRA_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/NovaInfra")
configure_package_config_file(
    cmake/NovaInfraConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/NovaInfraConfig.cmake"
    INSTALL_DESTINATION "${NOVAINFRA_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NovaInfraConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT NovaInfraTargets
    FILE NovaInfraTargets.cmake
    NAMESPACE NovaInfra::
    DESTINATION "${NOVAINFRA_INSTALL_CMAKEDIR}"
)
install(
    FILES
    "${CMAKE_CURRENT_BINARY_DIR}/NovaInfraConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/NovaInfraConfigVersion.cmake"
    DESTINATION "${NOVAINFRA_INSTALL_CMAKEDIR}"
)
install(
    FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/NovaInfra"
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
    )
endif()
