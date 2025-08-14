include(${CMAKE_SOURCE_DIR}/cmake/deps/catch2.cmake)

# Create test executable
add_executable(dd_native_tests
    impl/platform/filesystem_test.cpp
    impl/platform/http_test.cpp
    mock/filesystem_mock_test.cpp
)
target_enable_strict_warnings(dd_native_tests)
target_enable_coverage(dd_native_tests)

target_include_directories(dd_native_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/impl
    PRIVATE .
)

target_link_libraries(dd_native_tests
    PRIVATE
    dd_native_impl
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(dd_native_tests)
