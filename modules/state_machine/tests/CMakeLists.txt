if(NOT NOVAINFRA_NO_EXCEPTIONS)
    include(FetchContent)

    # 顶层构建没有 GTest 时，下载并构建固定版本的测试框架。
    if(NOT TARGET GTest::gtest_main)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()

    include(GoogleTest)
    # 运行状态图和 Runtime 的行为测试。
    add_executable(novainfra_state_machine_tests state_machine_tests.cpp)
    target_link_libraries(novainfra_state_machine_tests PRIVATE NovaInfra::StateMachine GTest::gtest_main)
    gtest_discover_tests(novainfra_state_machine_tests DISCOVERY_MODE PRE_TEST)
endif()

foreach(STATE_MACHINE_HEADER IN ITEMS state_machine/state_machine.h state_machine/runtime.h state_machine/types.h)
    # 为每个公共头文件生成独立编译目标，检查头文件自包含性。
    string(REPLACE "/" "_" STATE_MACHINE_HEADER_TARGET "${STATE_MACHINE_HEADER}")
    string(REPLACE "." "_" STATE_MACHINE_HEADER_TARGET "${STATE_MACHINE_HEADER_TARGET}")
    set(STATE_MACHINE_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/header_compile/${STATE_MACHINE_HEADER_TARGET}.cpp")
    file(GENERATE OUTPUT "${STATE_MACHINE_HEADER_SOURCE}"
        CONTENT "#include <${STATE_MACHINE_HEADER}>\nint main() { return 0; }\n")
    add_executable(novainfra_state_machine_header_${STATE_MACHINE_HEADER_TARGET} "${STATE_MACHINE_HEADER_SOURCE}")
    target_link_libraries(novainfra_state_machine_header_${STATE_MACHINE_HEADER_TARGET} PRIVATE NovaInfra::StateMachine)
    add_test(NAME "StateMachineHeader.${STATE_MACHINE_HEADER}" COMMAND novainfra_state_machine_header_${STATE_MACHINE_HEADER_TARGET})
endforeach()
