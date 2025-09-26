include(${DD_SDK_ROOT_DIR}/cmake/deps/catch2.cmake)

# Create test executable
add_executable(dd_native_tests
    c/attribute_c_api_test.cpp
    c/core_c_api_test.cpp
    c/logging_c_api_test.cpp
    c/rum_c_api_test.cpp
    cpp/attribute_cpp_api_test.cpp
    cpp/core_cpp_api_test.cpp
    cpp/logging_cpp_api_test.cpp
    cpp/rum_cpp_api_test.cpp
    impl/attribute/cow_test.cpp
    impl/attribute/json_test.cpp
    impl/attribute/memory_test.cpp
    impl/attribute/merge_test.cpp
    impl/attribute/refcount_test.cpp
    impl/core/block_test.cpp
    impl/core/context_test.cpp
    impl/core/core_test.cpp
    impl/core/feature_id_test.cpp
    impl/core/feature_read_test.cpp
    impl/core/feature_test.cpp
    impl/core/queue_test.cpp
    impl/core/site_test.cpp
    impl/core/storage_queue_test.cpp
    impl/core/storage_thread_test.cpp
    impl/core/storage_write_test.cpp
    impl/core/tlv_test.cpp
    impl/core/upload_scheduler_test.cpp
    impl/core/upload_thread_test.cpp
    impl/core/writer_test.cpp
    impl/platform/clock_test.cpp
    impl/platform/filesystem_test.cpp
    impl/platform/http_test.cpp
    impl/uuid_test.cpp
    mock/filesystem_mock_test.cpp
    mock/http_client_mock_test.cpp
    support/allocation_tracker.cpp
    support/attribute_debug.cpp
)
target_include_external_deps(dd_native_tests)
target_enable_strict_warnings(dd_native_tests)
target_enable_datadog_assert(dd_native_tests)
target_enable_coverage(dd_native_tests)
target_enable_sanitizers(dd_native_tests)

if(DD_ENABLE_TEST_ALLOCATION_TRACKING)
    target_compile_definitions(dd_native_tests PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=1)
else()
    target_compile_definitions(dd_native_tests PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=0)
endif()

# Strict checks in memory tests are automatically enabled on our default
# macOS/Clang/libc++ development environment; disabled by default everywhere else
if(DD_ENABLE_STRICT_MEMORY_CHECKS)
    target_compile_definitions(dd_native_tests PRIVATE WITH_DATADOG_STRICT_MEMORY_CHECKS=1)
endif()

# Asserts in thread-safety tests are disabled by default, as they're inherently
# timing-dependent and can therefore be flaky; but they can be explicitly enabled
if(DD_ENABLE_STRICT_THREADING_CHECKS)
    target_compile_definitions(dd_native_tests PRIVATE WITH_DATADOG_STRICT_THREADING_CHECKS=1)
endif()

target_include_directories(dd_native_tests
    PRIVATE ${DD_SDK_ROOT_DIR}/src/c
    PRIVATE ${DD_SDK_ROOT_DIR}/src/cpp
    PRIVATE ${DD_SDK_ROOT_DIR}/src/impl
    PRIVATE .
)

target_link_libraries(dd_native_tests
    PRIVATE
    dd_native
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(dd_native_tests)
