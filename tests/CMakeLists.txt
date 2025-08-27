include(${CMAKE_SOURCE_DIR}/cmake/deps/catch2.cmake)

# Create test executable
add_executable(dd_native_tests
    c/attribute_c_api_test.cpp
    cpp/attribute_cpp_api_test.cpp
    impl/attribute/cow_test.cpp
    impl/attribute/json_test.cpp
    impl/attribute/memory_test.cpp
    impl/attribute/merge_test.cpp
    impl/attribute/refcount_test.cpp
    impl/core/block_test.cpp
    impl/core/context_test.cpp
    impl/core/core_test.cpp
    impl/core/feature_test.cpp
    impl/core/queue_test.cpp
    impl/core/site_test.cpp
    impl/core/storage_test.cpp
    impl/core/tlv_test.cpp
    impl/core/upload_test.cpp
    impl/core/writer_test.cpp
    impl/platform/clock_test.cpp
    impl/platform/filesystem_test.cpp
    impl/platform/http_test.cpp
    mock/filesystem_mock_test.cpp
    mock/http_client_mock_test.cpp
    support/allocation_tracker.cpp
    support/attribute_debug.cpp
)
target_enable_strict_warnings(dd_native_tests)
target_enable_coverage(dd_native_tests)

option(DD_ENABLE_TEST_ALLOCATION_TRACKING "Enable allocation tracking (via instrumented new/delete) in test binaries" ON)
if(DD_ENABLE_TEST_ALLOCATION_TRACKING)
    target_compile_definitions(dd_native_tests PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=1)
else()
    target_compile_definitions(dd_native_tests PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=0)
endif()

target_include_directories(dd_native_tests
    PRIVATE ${CMAKE_SOURCE_DIR}/src/impl
    PRIVATE .
)

target_link_libraries(dd_native_tests
    PRIVATE
    dd_native_impl
    dd_native_c_api
    dd_native_cpp_api
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(dd_native_tests)
