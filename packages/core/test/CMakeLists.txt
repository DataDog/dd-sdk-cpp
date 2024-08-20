cmake_minimum_required(VERSION 3.21)

project(datadog-sdk-core-tests LANGUAGES CXX)

enable_testing()

Include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 31588bb4f56b638dd5afc28d3ebff9b9dcefb88d # Release 3.7.0
)

FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

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

include(CTest)
include(Catch)
catch_discover_tests(core-tests)
