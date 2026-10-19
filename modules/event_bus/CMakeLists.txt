option(NOVAINFRA_EVENT_BUS_BUILD_EXAMPLES "Build event_bus examples" ON)
option(NOVAINFRA_EVENT_BUS_BUILD_BENCHMARKS "Build event_bus microbenchmarks" OFF)

find_package(Threads REQUIRED)

add_library(novainfra_event_bus src/event_bus.cpp)
add_library(NovaInfra::EventBus ALIAS novainfra_event_bus)
target_compile_features(novainfra_event_bus PUBLIC cxx_std_17)
target_include_directories(novainfra_event_bus PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(novainfra_event_bus PUBLIC NovaInfra::Core Threads::Threads)
set_target_properties(novainfra_event_bus PROPERTIES EXPORT_NAME EventBus)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(novainfra_event_bus PRIVATE NOVAINFRA_LIB_EXPORT)
else()
    target_compile_definitions(novainfra_event_bus PUBLIC NOVAINFRA_STATIC_DEFINE)
endif()

install(TARGETS novainfra_event_bus EXPORT NovaInfraTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

if(NOVAINFRA_EVENT_BUS_BUILD_EXAMPLES)
    add_executable(example_event_bus examples/event_bus_example.cpp)
    target_link_libraries(example_event_bus PRIVATE NovaInfra::EventBus)

    add_executable(example_event_bus_multi_event examples/multi_event_example.cpp)
    target_link_libraries(example_event_bus_multi_event PRIVATE NovaInfra::EventBus)
endif()

if(NOVAINFRA_EVENT_BUS_BUILD_BENCHMARKS)
    add_executable(novainfra_event_bus_benchmarks benchmarks/event_bus_benchmarks.cpp)
    target_link_libraries(novainfra_event_bus_benchmarks PRIVATE NovaInfra::EventBus)

    if(MODULES_THREAD_POOL_ENABLE)
        target_link_libraries(novainfra_event_bus_benchmarks PRIVATE NovaInfra::ThreadPool)
        target_compile_definitions(novainfra_event_bus_benchmarks PRIVATE NOVAINFRA_BENCHMARK_THREAD_POOL)
    endif()
endif()
