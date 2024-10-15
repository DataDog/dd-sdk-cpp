cmake_minimum_required(VERSION 3.21)

project(datadog-sdk-core-tests LANGUAGES CXX)

enable_testing()

include(../../../cmake/deps/catch2.cmake)

add_executable(core-tests
    test_core.cpp
    
    storage/test_datadog_file_system.cpp
    storage/test_feature_storage.cpp
    storage/test_tlv_file_reader.cpp
)

target_include_directories(core-tests
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src

    ${CMAKE_SOURCE_DIR}/deps/trompeloeil/include
)
target_link_libraries(core-tests PRIVATE
    Catch2::Catch2WithMain
    datadog-core
)

include(CTest)
include(Catch)
catch_discover_tests(core-tests)
