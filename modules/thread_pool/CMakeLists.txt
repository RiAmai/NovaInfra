cmake_minimum_required(VERSION 3.12.0)

option(NOVAINFRA_THREAD_POOL_BUILD_EXAMPLES "Build thread_pool examples" ON)
option(NOVAINFRA_THREAD_POOL_BUILD_BENCHMARKS "Build thread_pool microbenchmarks" OFF)

find_package(Threads REQUIRED)

file(GLOB THREAD_POOL_PUBLIC_HEADERS "include/thread_pool/*.h")
file(GLOB THREAD_POOL_SRCS "src/*.cpp")

add_library(novainfra_thread_pool ${THREAD_POOL_SRCS} ${THREAD_POOL_PUBLIC_HEADERS})
add_library(NovaInfra::ThreadPool ALIAS novainfra_thread_pool)

target_compile_features(novainfra_thread_pool PUBLIC cxx_std_17)
target_include_directories(novainfra_thread_pool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(novainfra_thread_pool PUBLIC NovaInfra::Core Threads::Threads)
set_target_properties(novainfra_thread_pool PROPERTIES EXPORT_NAME ThreadPool)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(novainfra_thread_pool PRIVATE NOVAINFRA_LIB_EXPORT)
else()
    target_compile_definitions(novainfra_thread_pool PUBLIC NOVAINFRA_STATIC_DEFINE)
endif()

install(TARGETS novainfra_thread_pool
    EXPORT NovaInfraTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL AND NOT NOVAINFRA_NO_EXCEPTIONS)
    add_subdirectory(tests)
endif()

if(NOVAINFRA_THREAD_POOL_BUILD_EXAMPLES)
    add_executable(example_thread_pool examples/thread_pool_example.cpp)
    target_link_libraries(example_thread_pool PRIVATE NovaInfra::ThreadPool)

    add_executable(example_thread_pool_real_world examples/real_world_example.cpp)
    target_link_libraries(example_thread_pool_real_world PRIVATE NovaInfra::ThreadPool)
endif()

if(NOVAINFRA_THREAD_POOL_BUILD_BENCHMARKS)
    add_executable(novainfra_thread_pool_benchmarks benchmarks/thread_pool_benchmarks.cpp)
    target_link_libraries(novainfra_thread_pool_benchmarks PRIVATE NovaInfra::ThreadPool Threads::Threads)
endif()
