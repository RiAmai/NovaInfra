include(FetchContent)

if(NOT TARGET GTest::gtest_main)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(novainfra_core_mpmc_queue_tests mpmc_queue_tests.cpp)
target_link_libraries(novainfra_core_mpmc_queue_tests PRIVATE NovaInfra::Core GTest::gtest_main)

add_executable(novainfra_core_circular_queue_tests circular_queue_tests.cpp)
target_link_libraries(novainfra_core_circular_queue_tests PRIVATE NovaInfra::Core GTest::gtest_main)

add_executable(novainfra_core_singleton_tests singleton_tests.cpp)
target_link_libraries(novainfra_core_singleton_tests PRIVATE NovaInfra::Core GTest::gtest_main)

add_executable(novainfra_core_os_utils_tests os_utils_tests.cpp)
target_link_libraries(novainfra_core_os_utils_tests PRIVATE NovaInfra::Core GTest::gtest_main)

gtest_discover_tests(novainfra_core_mpmc_queue_tests DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(novainfra_core_circular_queue_tests DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(novainfra_core_singleton_tests DISCOVERY_MODE PRE_TEST)
gtest_discover_tests(novainfra_core_os_utils_tests DISCOVERY_MODE PRE_TEST)
