cmake_minimum_required(VERSION 3.14)
project(fox-http CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    # Use CACHE STRING ... FORCE so CMakeCache.txt reflects the effective
    # build type. Plain set() without CACHE only updates the local variable;
    # compile flags would still be correct, but `cmake -LA` and readers of
    # the cache would see an empty CMAKE_BUILD_TYPE and get confused.
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type (Debug/Release/RelWithDebInfo/MinSizeRel)" FORCE)
endif()

option(FOX_HTTP_BUILD_TESTS "Build fox-http tests and examples" ON)
option(FOX_HTTP_DEBUG_LOG "Enable FOX_HTTP_LOG output" OFF)

# Boost.Asio is header-only in modern Boost (>=1.87); we still need the
# headers. Link against Boost::headers (provided by CMake Boost config) for
# portable include paths.
find_package(Boost REQUIRED)
find_package(Threads REQUIRED)

# ─── Library ─────────────────────────────────────────────────────

add_library(fox-http STATIC
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
add_library(fox-http::fox-http ALIAS fox-http)

target_include_directories(fox-http
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(fox-http
    PUBLIC
        Boost::headers
        Threads::Threads
)

target_compile_options(fox-http PRIVATE -Wall -Wextra)

if(FOX_HTTP_DEBUG_LOG)
    target_compile_definitions(fox-http PRIVATE FOX_HTTP_DEBUG_LOG)
endif()

set_target_properties(fox-http PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# ─── Tests / examples ────────────────────────────────────────────

if(FOX_HTTP_BUILD_TESTS)
    add_executable(hello_world test/hello_world.cpp)
    target_link_libraries(hello_world PRIVATE fox-http::fox-http)

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
            fox-http::fox-http
            GTest::gtest
            GTest::gtest_main
        )
        target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        include(GoogleTest)
        gtest_discover_tests(unit_tests)
    else()
        message(STATUS "GTest not found; skipping unit_tests target")
    endif()

    # Integration test: chunked request body round-trip. Spins up hello_world
    # on an ephemeral-ish port, runs the python client, tears down.
    find_program(PYTHON3 python3)
    if(PYTHON3 AND TARGET hello_world)
        add_test(NAME ChunkedBodyIntegration
            COMMAND ${CMAKE_COMMAND} -E env
                HELLO_WORLD=$<TARGET_FILE:hello_world>
                PYTHON=${PYTHON3}
                sh -c "$<TARGET_FILE:hello_world> 18099 >/dev/null 2>&1 & SRV=$!; sleep 0.5; ${PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/test/integration/chunked_body_test.py --port 18099; RC=$?; kill $SRV 2>/dev/null; exit $RC"
        )
    endif()
endif()

# ─── Install ─────────────────────────────────────────────────────

include(GNUInstallDirs)

install(TARGETS fox-http
    EXPORT fox-httpTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/fox-http
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT fox-httpTargets
    FILE fox-httpTargets.cmake
    NAMESPACE fox-http::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fox-http
)
