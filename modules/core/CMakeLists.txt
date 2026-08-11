add_library(nova_core INTERFACE)
add_library(Nova::Core ALIAS nova_core)

target_compile_features(nova_core INTERFACE cxx_std_17)
target_include_directories(nova_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(nova_core PROPERTIES EXPORT_NAME Core)

install(TARGETS nova_core EXPORT NovaCoreTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
