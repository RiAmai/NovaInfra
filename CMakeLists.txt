cmake_minimum_required(VERSION 3.10.0)
project(NovaCore VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(MODULES_CORE_ENABLE ON CACHE BOOL "Enable core module" FORCE)
set(MODULES_LOGGER_ENABLE ON CACHE BOOL "Enable logger module" FORCE)
set(MODULES_EVENT_BUS_ENABLE ON CACHE BOOL "Enable event bus module" FORCE)
set(MODULES_CONFIG_ENABLE ON CACHE BOOL "Enable config module" FORCE)
set(MODULES_THREAD_POOL_ENABLE ON CACHE BOOL "Enable thread pool module" FORCE)
set(MODULES_SQLITE_POOL_ENABLE ON CACHE BOOL "Enable sqlite pool module" FORCE)
set(MODULES_SIMULATOR_ENABLE ON CACHE BOOL "Enable simulator module" FORCE)


if (MODULES_CORE_ENABLE)
    add_subdirectory(modules/core)
endif()
if (MODULES_LOGGER_ENABLE)
    add_subdirectory(modules/logger)
endif()
if (MODULES_EVENT_BUS_ENABLE)
    add_subdirectory(modules/event_bus)
endif()
if (MODULES_CONFIG_ENABLE)
    add_subdirectory(modules/config)
endif()
if (MODULES_THREAD_POOL_ENABLE)
    add_subdirectory(modules/thread_pool)
endif()
if (MODULES_SQLITE_POOL_ENABLE)
    add_subdirectory(modules/sqlite_pool)
endif()
if (MODULES_SIMULATOR_ENABLE)
    add_subdirectory(modules/simulator)
endif()



