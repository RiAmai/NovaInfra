cmake_minimum_required(VERSION 3.21)
project(NovaCore VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "NovaCore only supports Linux")
endif()

if(PROJECT_IS_TOP_LEVEL AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(
        CMAKE_INSTALL_PREFIX
        "${PROJECT_SOURCE_DIR}/install/novacore/"
        CACHE PATH "NovaCore install prefix" FORCE
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(MODULES_LOGGER_ENABLE "Enable logger module" ON)
option(MODULES_EVENT_BUS_ENABLE "Enable event bus module" ON)
option(MODULES_CONFIG_ENABLE "Enable config module" ON)
option(MODULES_THREAD_POOL_ENABLE "Enable thread pool module" ON)
option(MODULES_SQLITE_POOL_ENABLE "Enable sqlite pool module" ON)
option(MODULES_SIMULATOR_ENABLE "Enable simulator module" ON)

option(NOVA_LOGGER_BUILD_EXAMPLES "Build logger examples" ON)
option(NOVA_LOGGER_BUILD_BENCHMARKS "Build logger microbenchmarks" OFF)
option(NOVA_NO_EXCEPTIONS "Build NovaCore without C++ exceptions" OFF)
option(NOVA_NO_TZ_OFFSET "Omit %z timezone offset" OFF)
option(NOVA_PREVENT_CHILD_FD "Prevent child processes from inheriting log file descriptors" OFF)
option(NOVA_NO_THREAD_ID "Disable thread-id queries on log calls" OFF)
option(NOVA_NO_TLS "Disable thread-local storage" OFF)
option(NOVA_CLOCK_COARSE "Use CLOCK_REALTIME_COARSE" OFF)

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
if(MODULES_SIMULATOR_ENABLE)
    add_subdirectory(modules/simulator)
endif()

foreach(NOVA_OPTION IN ITEMS
        NOVA_CLOCK_COARSE
        NOVA_NO_EXCEPTIONS
        NOVA_NO_THREAD_ID
        NOVA_NO_TLS
        NOVA_NO_TZ_OFFSET
        NOVA_PREVENT_CHILD_FD)
    if(${NOVA_OPTION})
        target_compile_definitions(nova_core INTERFACE ${NOVA_OPTION})
    endif()
endforeach()

if(NOVA_NO_EXCEPTIONS)
    target_compile_options(nova_core INTERFACE -fno-exceptions)
endif()

set(NOVACORE_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/NovaCore")
configure_package_config_file(
    cmake/NovaCoreConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/NovaCoreConfig.cmake"
    INSTALL_DESTINATION "${NOVACORE_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/NovaCoreConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    EXPORT NovaCoreTargets
    FILE NovaCoreTargets.cmake
    NAMESPACE Nova::
    DESTINATION "${NOVACORE_INSTALL_CMAKEDIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/NovaCoreConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/NovaCoreConfigVersion.cmake"
    DESTINATION "${NOVACORE_INSTALL_CMAKEDIR}"
)
install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/NovaCore"
)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/build/compile_commands.json"
    )
endif()
