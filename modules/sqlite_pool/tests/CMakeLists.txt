if(NOT NOVAINFRA_NO_EXCEPTIONS)
    include(FetchContent)

    if(NOT TARGET GTest::gtest_main)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        FetchContent_MakeAvailable(googletest)
    endif()

    include(GoogleTest)
    add_executable(novainfra_sqlite_pool_tests sqlite_pool_tests.cpp)
    target_link_libraries(novainfra_sqlite_pool_tests PRIVATE NovaInfra::SqlitePool GTest::gtest_main)
    gtest_discover_tests(novainfra_sqlite_pool_tests DISCOVERY_MODE PRE_TEST)
endif()

foreach(SQLITE_POOL_HEADER IN ITEMS sqlite_pool/connection_pool.h sqlite_pool/read_conn.h sqlite_pool/write_conn.h sqlite_pool/statement.h sqlite_pool/sqlite_error.h sqlite_pool/types.h)
    string(REPLACE "/" "_" SQLITE_POOL_HEADER_TARGET "${SQLITE_POOL_HEADER}")
    string(REPLACE "." "_" SQLITE_POOL_HEADER_TARGET "${SQLITE_POOL_HEADER_TARGET}")
    set(SQLITE_POOL_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/header_compile/${SQLITE_POOL_HEADER_TARGET}.cpp")
    file(GENERATE OUTPUT "${SQLITE_POOL_HEADER_SOURCE}"
        CONTENT "#include <${SQLITE_POOL_HEADER}>\nint main() { return 0; }\n")
    add_executable(novainfra_sqlite_pool_header_${SQLITE_POOL_HEADER_TARGET} "${SQLITE_POOL_HEADER_SOURCE}")
    target_link_libraries(novainfra_sqlite_pool_header_${SQLITE_POOL_HEADER_TARGET} PRIVATE NovaInfra::SqlitePool)
    add_test(NAME "SqlitePoolHeader.${SQLITE_POOL_HEADER}" COMMAND novainfra_sqlite_pool_header_${SQLITE_POOL_HEADER_TARGET})
endforeach()
