option(NOVAINFRA_CORE_BUILD_EXAMPLES "Build core examples" ON)

add_library(novainfra_core INTERFACE)
add_library(NovaInfra::Core ALIAS novainfra_core)

target_compile_features(novainfra_core INTERFACE cxx_std_17)
target_include_directories(novainfra_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(novainfra_core PROPERTIES EXPORT_NAME Core)

install(TARGETS novainfra_core EXPORT NovaInfraTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

if(NOVAINFRA_CORE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
