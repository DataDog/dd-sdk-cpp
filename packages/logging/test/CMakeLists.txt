cmake_minimum_required(VERSION 3.21)

project(datadog-sdk-logging-tests LANGUAGES CXX)

enable_testing()

include(../../../cmake/deps/catch2.cmake)

add_executable(logging-tests
    test_logging_feature.cpp
)

target_include_directories(logging-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common_test/src

    ${CMAKE_SOURCE_DIR}/deps/trompeloeil/include
    ${CMAKE_SOURCE_DIR}/deps/rapidjson/include
)
target_link_libraries(logging-tests PRIVATE
    Catch2::Catch2WithMain
    common_options
    datadog-core
    datadog-logging
)

include(CTest)
include(Catch)
catch_discover_tests(logging-tests)
