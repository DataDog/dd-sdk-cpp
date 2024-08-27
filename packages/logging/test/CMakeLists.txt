cmake_minimum_required(VERSION 3.21)

project(datadog-sdk-logging-tests LANGUAGES CXX)

enable_testing()

include(../../../cmake/inc/catch2.cmake)

add_executable(logger-tests
    test_logger.cpp
)

target_include_directories(logger-tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/src
    # Include mocks from core tests
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/test/mocks
)
target_link_libraries(logger-tests PRIVATE
    Catch2::Catch2WithMain
    datadog-logging
)

catch_discover_tests(logger-tests)
