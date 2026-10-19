option(NOVAINFRA_SQLITE_POOL_BUILD_EXAMPLES "Build sqlite_pool examples" ON)
option(NOVAINFRA_SQLITE_POOL_BUILD_BENCHMARKS "Build sqlite_pool microbenchmarks" OFF)

set(SQLITE_BACKEND "plain" CACHE STRING
    "SQLite backend: plain | sqlcipher | auto")
set_property(CACHE SQLITE_BACKEND PROPERTY STRINGS plain sqlcipher auto)

if(NOT APPLE)
    set(_default_crypto "openssl")
else()
    set(_default_crypto "commoncrypto")
endif()

set(SQLCIPHER_CRYPTO "${_default_crypto}" CACHE STRING
    "Crypto backend for bundled SQLiteCipher: openssl | commoncrypto | libtomcrypt")
set_property(CACHE SQLCIPHER_CRYPTO PROPERTY STRINGS openssl commoncrypto libtomcrypt)

# Platform-specific compiler options
if(MSVC)
    add_compile_options(/W4 /WX /EHsc /MP /utf-8 /wd4100)
    add_compile_options(
        $<$<CONFIG:Release>:/O2 /GL>
        $<$<CONFIG:Debug>:/Od /Zi /RTC1>
    )
    add_link_options($<$<CONFIG:Release>:/LTCG>)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Debug>:-g>
    )
endif()

# Helper: compile the bundled amalgamation
# Works for both plain sqlite3.c and sqlcipher's sqlite3.c
# extra_defs: list of compile definitions to add (e.g. SQLITE_HAS_CODEC)
# extra_libs: list of libraries to link (e.g. OpenSSL::Crypto).
function(add_sqlite_amalgamation target_name extra_defs extra_libs)
    set(_src "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c")
    set(_hdr "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h")

    if(NOT EXISTS "${_src}" OR NOT EXISTS "${_hdr}")
        message(FATAL_ERROR
            "Bundled amalgamation not found (sqlite3.h / sqlite3.c).\n"
            "For plain SQLite: https://www.sqlite.org/download.html\n"
            "For SQLiteCipher: https://github.com/sqlcipher/sqlcipher")
    endif()

    enable_language(C)
    add_library(${target_name} STATIC "${_src}")
    target_include_directories(${target_name} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    install(TARGETS ${target_name} EXPORT NovaInfraTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES "${_hdr}" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    target_compile_definitions(${target_name} PUBLIC
        SQLITE_THREADSAFE=2
        SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
        SQLITE_ENABLE_JSON1
        SQLITE_ENABLE_FTS5
        SQLITE_ENABLE_RTREE
        SQLITE_USE_URI=1
        ${extra_defs})

    if(extra_libs)
        target_link_libraries(${target_name} PUBLIC ${extra_libs})
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W0)
    else()
        target_compile_options(${target_name} PRIVATE -w)
    endif()
endfunction()

# Helper: resolve crypto library for bundled SQLiteCipher
function(resolve_sqlcipher_crypto out_def out_lib)
    if(SQLCIPHER_CRYPTO STREQUAL "openssl")
        find_package(OpenSSL REQUIRED COMPONENTS Crypto)
        set(${out_def} "SQLCIPHER_CRYPTO_OPENSSL" PARENT_SCOPE)
        set(${out_lib} "OpenSSL::Crypto" PARENT_SCOPE)
    elseif(SQLCIPHER_CRYPTO STREQUAL "commoncrypto")
        # CommonCrypto ships with macOS; no extra package is needed.
        set(${out_def} "SQLCIPHER_CRYPTO_CC" PARENT_SCOPE)
        set(${out_lib} "" PARENT_SCOPE)
    elseif(SQLCIPHER_CRYPTO STREQUAL "libtomcrypt")
        find_library(LIBTOMCRYPT_LIB tomcrypt REQUIRED)
        set(${out_def} "SQLCIPHER_CRYPTO_LIBTOMCRYPT" PARENT_SCOPE)
        set(${out_lib} "${LIBTOMCRYPT_LIB}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "Unknown SQLCIPHER_CRYPTO value: ${SQLCIPHER_CRYPTO}")
    endif()
endfunction()

# Resolve the actual SQLite target :
set(NOVAINFRA_SQLITE_DEPENDENCY "")
set(NOVAINFRA_SQLITE_CRYPTO_DEPENDENCY "")
set(_use_cipher FALSE)
set(_sqlite_target "") # will be set to the final CMake target name
set(_sqlite_include_dirs "")
set(_sqlite_link_libraries "")
set(_sqlite_link_directories "")

# SQLiteCipher branch
if(SQLITE_BACKEND STREQUAL "sqlcipher" OR SQLITE_BACKEND STREQUAL "auto")
    # try pkg-config
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(PKGSQLCIPHER QUIET sqlcipher)
    endif()

    if(PKGSQLCIPHER_FOUND)
        message(STATUS "Using system SQLiteCipher ${PKGSQLCIPHER_VERSION} (pkg-config)")
        set(_sqlite_include_dirs ${PKGSQLCIPHER_INCLUDE_DIRS})
        set(_sqlite_link_directories ${PKGSQLCIPHER_LIBRARY_DIRS})
        set(_sqlite_link_libraries ${PKGSQLCIPHER_LIBRARIES})
        set(_use_cipher TRUE)
    else()
        # try find_library
        find_library(_sqlcipher_lib NAMES sqlcipher)
        find_path(_sqlcipher_inc NAMES sqlite3.h PATH_SUFFIXES sqlcipher)

        if(_sqlcipher_lib AND _sqlcipher_inc)
            message(STATUS "Using system SQLiteCipher at ${_sqlcipher_lib}")
            set(_sqlite_include_dirs "${_sqlcipher_inc}")
            set(_sqlite_link_libraries "${_sqlcipher_lib}")
            set(_use_cipher TRUE)
        else()
            # bundled amalgamation
            if(SQLITE_BACKEND STREQUAL "sqlcipher")
                message(STATUS
                    "No system SQLiteCipher found - compiling bundled amalgamation "
                    "(crypto=${SQLCIPHER_CRYPTO})")
                resolve_sqlcipher_crypto(_cipher_def _cipher_lib)
                add_sqlite_amalgamation(sqlcipher_amalgamation
                    "SQLITE_HAS_CODEC;${_cipher_def}" "${_cipher_lib}")
                set(_sqlite_target "sqlcipher_amalgamation")

                if(SQLCIPHER_CRYPTO STREQUAL "openssl")
                    set(NOVAINFRA_SQLITE_CRYPTO_DEPENDENCY "OpenSSL")
                endif()

                set(_use_cipher TRUE)
            else()
                message(STATUS "SQLiteCipher not found; falling back to plain SQLite")
            endif()
        endif()
    endif()
endif()

# Plain SQLite branch (also fallback for auto)
if(NOT _use_cipher)
    set(_plain_found FALSE)

    # CMake FindSQLite3
    find_package(SQLite3 QUIET)

    if(SQLite3_FOUND)
        message(STATUS "Using system SQLite ${SQLite3_VERSION}")
        set(_sqlite_target "SQLite::SQLite3")
        set(NOVAINFRA_SQLITE_DEPENDENCY "SQLite3")
        set(_plain_found TRUE)
    endif()

    # pkg_config
    if(NOT _plain_found)
        find_package(PkgConfig QUIET)

        if(PkgConfig_FOUND)
            pkg_check_modules(PKGSQLITE3 QUIET sqlite3)
        endif()

        if(PKGSQLITE3_FOUND)
            message(STATUS "Using system SQLite ${PKGSQLITE3_VERSION} (pkg-config)")
            add_library(_sqlite3_pkgcfg INTERFACE IMPORTED)
            target_include_directories(_sqlite3_pkgcfg INTERFACE ${PKGSQLITE3_INCLUDE_DIRS})
            target_link_libraries(_sqlite3_pkgcfg INTERFACE ${PKGSQLITE3_LIBRARIES})
            target_link_directories(_sqlite3_pkgcfg INTERFACE ${PKGSQLITE3_LIBRARY_DIRS})
            set(_sqlite_target "_sqlite3_pkgcfg")
            set(NOVAINFRA_SQLITE_DEPENDENCY "sqlite-pkg")
            set(_plain_found TRUE)
        endif()
    endif()

    # bundled amalgamation
    if(NOT _plain_found)
        message(STATUS "No system SQLite found - compiling bundled amalgamation")
        add_sqlite_amalgamation(sqlite3_amalgamation "" "")
        set(_sqlite_target "sqlite3_amalgamation")
    endif()
endif()

if(_use_cipher AND NOT _sqlite_target)
    set(NOVAINFRA_SQLITE_DEPENDENCY "sqlcipher")
    add_library(NovaInfraSQLiteBackend INTERFACE IMPORTED GLOBAL)
    set_target_properties(NovaInfraSQLiteBackend PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${_sqlite_include_dirs}"
        INTERFACE_LINK_LIBRARIES "${_sqlite_link_libraries}"
        INTERFACE_LINK_DIRECTORIES "${_sqlite_link_directories}")
    set(_sqlite_target NovaInfraSQLiteBackend)
    set(_sqlite_include_dirs "")
    set(_sqlite_link_libraries "")
    set(_sqlite_link_directories "")
endif()

set(NOVAINFRA_SQLITE_DEPENDENCY "${NOVAINFRA_SQLITE_DEPENDENCY}" PARENT_SCOPE)
set(NOVAINFRA_SQLITE_CRYPTO_DEPENDENCY "${NOVAINFRA_SQLITE_CRYPTO_DEPENDENCY}" PARENT_SCOPE)

# -- Propagate the SQLITEPOOL_USE_SQLCIPHER definition to the pool -
# Pool code can use #ifdef SQLITEPOOL_USE_SQLCIPHER to
# add behavior specific to the encryption algorithm.
if(_use_cipher)
    message(STATUS "sqlite_pool: encryption ENABLED (SQLiteCipher)")
else()
    message(STATUS "sqlite_pool: encryption DISABLED (plain SQLite)")
endif()

# pool library
file(GLOB SQLITEPOOL_SRCS "src/*.cpp")

add_library(novainfra_sqlitepool ${SQLITEPOOL_SRCS})
add_library(NovaInfra::SqlitePool ALIAS novainfra_sqlitepool)
target_compile_features(novainfra_sqlitepool PUBLIC cxx_std_17)
target_include_directories(novainfra_sqlitepool PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(novainfra_sqlitepool PUBLIC NovaInfra::Core "${_sqlite_target}" ${_sqlite_link_libraries})

if(_sqlite_include_dirs)
    target_include_directories(novainfra_sqlitepool PUBLIC ${_sqlite_include_dirs})
endif()

if(_sqlite_link_directories)
    target_link_directories(novainfra_sqlitepool PUBLIC ${_sqlite_link_directories})
endif()

set_target_properties(novainfra_sqlitepool PROPERTIES EXPORT_NAME SqlitePool)

if(_use_cipher)
    target_compile_definitions(novainfra_sqlitepool PUBLIC SQLITEPOOL_USE_SQLCIPHER)
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(novainfra_sqlitepool PUBLIC Threads::Threads)
endif()

# install rules
install(TARGETS novainfra_sqlitepool
    EXPORT NovaInfraTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(NOVAINFRA_SQLITE_POOL_BUILD_EXAMPLES)
    add_executable(example_sqlite_pool examples/sqlite_pool_example.cpp)
    target_link_libraries(example_sqlite_pool PRIVATE NovaInfra::SqlitePool)
endif()

if(NOVAINFRA_SQLITE_POOL_BUILD_BENCHMARKS)
    add_executable(novainfra_sqlitepool_benchmarks benchmarks/sqlite_pool_benchmarks.cpp)
    target_link_libraries(novainfra_sqlitepool_benchmarks PRIVATE NovaInfra::SqlitePool Threads::Threads)
endif()

if(BUILD_TESTING AND PROJECT_IS_TOP_LEVEL)
    add_subdirectory(tests)
endif()
