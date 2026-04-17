cmake_minimum_required(VERSION 3.14)
project(httpserver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(HTTPSERVER_BUILD_TESTS "Build httpserver tests and examples" ON)
option(HTTPSERVER_DEBUG_LOG "Enable HTTPSERVER_LOG output" OFF)

# Boost.Asio is header-only in modern Boost (>=1.87); we still need the
# headers. Link against Boost::headers (provided by CMake Boost config) for
# portable include paths.
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

# ─── Library ─────────────────────────────────────────────────────

add_library(httpserver STATIC
    src/http_request.cpp
    src/http_response.cpp
    src/http_server.cpp
    src/http_router.cpp
    src/http_util.cpp
    src/connection.cpp
    src/timer_manager.cpp
    src/server_status.cpp
)

# Response streaming uses iovec/writev from <sys/uio.h> — POSIX.
add_library(httpserver::httpserver ALIAS httpserver)

target_include_directories(httpserver
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(httpserver
    PUBLIC
        Boost::headers
        Threads::Threads
)

target_compile_options(httpserver PRIVATE -Wall -Wextra)

if(HTTPSERVER_DEBUG_LOG)
    target_compile_definitions(httpserver PRIVATE HTTPSERVER_DEBUG_LOG)
endif()

set_target_properties(httpserver PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ─── Tests / examples ────────────────────────────────────────────

if(HTTPSERVER_BUILD_TESTS)
    add_executable(hello_world test/hello_world.cpp)
    target_link_libraries(hello_world PRIVATE httpserver::httpserver)

    find_package(GTest)
    if(GTest_FOUND)
        enable_testing()
        add_executable(unit_tests
            test/router_test.cpp
            test/util_test.cpp
            test/request_test.cpp
            test/response_test.cpp
        )
        target_link_libraries(unit_tests PRIVATE
            httpserver::httpserver
            GTest::gtest
            GTest::gtest_main
        )
        target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        include(GoogleTest)
        gtest_discover_tests(unit_tests)
    else()
        message(STATUS "GTest not found; skipping unit_tests target")
    endif()
endif()

# ─── Install ─────────────────────────────────────────────────────

include(GNUInstallDirs)

install(TARGETS httpserver
    EXPORT httpserverTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/httpserver
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT httpserverTargets
    FILE httpserverTargets.cmake
    NAMESPACE httpserver::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/httpserver
)
