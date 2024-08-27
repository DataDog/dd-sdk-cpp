cmake_minimum_required(VERSION 3.21)

project(datadog-sdk-core-tests LANGUAGES CXX)

enable_testing()

include(../../../cmake/inc/catch2.cmake)

add_executable(core-tests
    test_core.cpp
)

target_include_directories(core-tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_link_libraries(core-tests PRIVATE
    Catch2::Catch2WithMain
    datadog-core
)

catch_discover_tests(core-tests)
