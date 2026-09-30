# 是否构建状态机示例程序。
option(NOVAINFRA_STATE_MACHINE_BUILD_EXAMPLES "Build state machine examples" ON)

# 状态机核心为 header-only 接口，底层实现和 Runtime 都在头文件中。
add_library(novainfra_state_machine INTERFACE)
add_library(NovaInfra::StateMachine ALIAS novainfra_state_machine)
target_compile_features(novainfra_state_machine INTERFACE cxx_std_17)
target_include_directories(novainfra_state_machine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(novainfra_state_machine INTERFACE NovaInfra::Core)
set_target_properties(novainfra_state_machine PROPERTIES EXPORT_NAME StateMachine)

# 安装目标和公共头文件，包含 vendored 的 Boost.SML 单头文件。
install(TARGETS novainfra_state_machine EXPORT NovaInfraTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()

if(NOVAINFRA_STATE_MACHINE_BUILD_EXAMPLES)
    # 示例展示 typed event 和外部 executor 的基本用法。
    add_executable(example_state_machine examples/state_machine_example.cpp)
    target_link_libraries(example_state_machine PRIVATE NovaInfra::StateMachine)
endif()
