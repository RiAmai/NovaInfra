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

add_executable(novainfra_sqlitepool_tests sqlite_pool_tests.cpp)
target_link_libraries(novainfra_sqlitepool_tests PRIVATE NovaInfra::SqlitePool GTest::gtest_main)
gtest_discover_tests(novainfra_sqlitepool_tests DISCOVERY_MODE PRE_TEST)
