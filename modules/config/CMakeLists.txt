include(FetchContent)

option(NOVAINFRA_CONFIG_BUILD_EXAMPLES "Build config examples" ON)
option(NOVAINFRA_CONFIG_BUILD_TOOLS "Build config tools" OFF)
option(NOVAINFRA_CONFIG_BUILD_BENCHMARKS "Build config microbenchmarks" "${BUILD_BENCHMARKS}")

FetchContent_Declare(
    tomlplusplus
    URL https://raw.githubusercontent.com/marzer/tomlplusplus/v3.4.0/toml.hpp
    URL_HASH SHA256=6b5172ad4dd6519aec67b919181fa7a38a2234131e5b2afa232dfe444819783e
    DOWNLOAD_NAME toml.hpp
    DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_GetProperties(tomlplusplus)

if(NOT tomlplusplus_POPULATED)
    FetchContent_Populate(tomlplusplus)
endif()

file(GLOB CONFIG_PUBLIC_HEADERS "include/config/*.h")
add_library(novainfra_config
    src/config_manager.cpp
    src/environment.cpp
    src/error.cpp
    src/schema.cpp
    src/value.cpp
    ${CONFIG_PUBLIC_HEADERS}
)
add_library(NovaInfra::Config ALIAS novainfra_config)
target_compile_features(novainfra_config PUBLIC cxx_std_17)
target_include_directories(novainfra_config PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(novainfra_config PRIVATE "${tomlplusplus_SOURCE_DIR}")
target_compile_definitions(novainfra_config PRIVATE TOML_EXCEPTIONS=0 TOML_HEADER_ONLY=1)
target_link_libraries(novainfra_config PUBLIC NovaInfra::Core)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(novainfra_config PUBLIC Threads::Threads)
endif()

set_target_properties(novainfra_config PROPERTIES EXPORT_NAME Config)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(novainfra_config PRIVATE NOVAINFRA_LIB_EXPORT)
else()
    target_compile_definitions(novainfra_config PUBLIC NOVAINFRA_STATIC_DEFINE)
endif()

install(TARGETS novainfra_config EXPORT NovaInfraTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

if(NOVAINFRA_CONFIG_BUILD_EXAMPLES)
    add_executable(example_config examples/config_example.cpp)
    target_link_libraries(example_config PRIVATE NovaInfra::Config)

    add_executable(example_config_watch examples/config_watch_example.cpp)
    target_link_libraries(example_config_watch PRIVATE NovaInfra::Config)

    add_executable(example_config_validation examples/config_validation_example.cpp)
    target_link_libraries(example_config_validation PRIVATE NovaInfra::Config)
endif()

if(NOVAINFRA_CONFIG_BUILD_BENCHMARKS OR BUILD_BENCHMARKS)
    add_executable(novainfra_config_benchmarks benchmarks/config_benchmarks.cpp)
    target_link_libraries(novainfra_config_benchmarks PRIVATE NovaInfra::Config)
endif()

# Config tools
if(NOVAINFRA_CONFIG_BUILD_TOOLS)
    add_executable(config_template_generator tools/config_template_generator.cpp)
    target_link_libraries(config_template_generator PRIVATE NovaInfra::Config)

    add_executable(config_validator tools/config_validator.cpp)
    target_link_libraries(config_validator PRIVATE NovaInfra::Config)
endif()
