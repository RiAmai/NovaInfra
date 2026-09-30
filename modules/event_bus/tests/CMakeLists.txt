find_package(Threads REQUIRED)

if(NOT NOVAINFRA_NO_EXCEPTIONS)
    include(FetchContent)

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
    add_executable(novainfra_event_bus_tests event_bus_tests.cpp)
    target_link_libraries(novainfra_event_bus_tests PRIVATE NovaInfra::EventBus GTest::gtest_main Threads::Threads)
    gtest_discover_tests(novainfra_event_bus_tests DISCOVERY_MODE PRE_TEST)
endif()

foreach(EVENT_BUS_HEADER IN ITEMS event_bus/event_bus.h event_bus/subscription.h event_bus/types.h)
    string(REPLACE "/" "_" EVENT_BUS_HEADER_TARGET "${EVENT_BUS_HEADER}")
    string(REPLACE "." "_" EVENT_BUS_HEADER_TARGET "${EVENT_BUS_HEADER_TARGET}")
    set(EVENT_BUS_HEADER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/header_compile/${EVENT_BUS_HEADER_TARGET}.cpp")
    file(GENERATE OUTPUT "${EVENT_BUS_HEADER_SOURCE}"
        CONTENT "#include <${EVENT_BUS_HEADER}>\nint main() { return 0; }\n")
    add_executable(novainfra_event_bus_header_${EVENT_BUS_HEADER_TARGET} "${EVENT_BUS_HEADER_SOURCE}")
    target_link_libraries(novainfra_event_bus_header_${EVENT_BUS_HEADER_TARGET} PRIVATE NovaInfra::EventBus)
    add_test(NAME "EventBusHeader.${EVENT_BUS_HEADER}" COMMAND novainfra_event_bus_header_${EVENT_BUS_HEADER_TARGET})
endforeach()
