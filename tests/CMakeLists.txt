# Include development-only dependencies
include(${DD_SDK_ROOT_DIR}/cmake/deps/catch2.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/deps/nlohmann-json.cmake)

# Create test executable
add_executable(dd_native_tests
    c/attribute_c_api_test.cpp
    c/core_c_api_test.cpp
    c/diagnostics_c_api_test.cpp
    c/logging_c_api_test.cpp
    c/rum_c_api_test.cpp
    c/timestamp_c_api_test.cpp
    c/user_info_c_api_test.cpp
    c/uuid_c_api_test.cpp
    c/version_c_api_test.cpp
    cpp/attribute_cpp_api_test.cpp
    cpp/core_cpp_api_test.cpp
    cpp/diagnostics_cpp_api_test.cpp
    cpp/logging_cpp_api_test.cpp
    cpp/rum_cpp_api_test.cpp
    cpp/timestamp_cpp_api_test.cpp
    cpp/user_info_cpp_api_test.cpp
    cpp/uuid_cpp_api_test.cpp
    cpp/version_cpp_api_test.cpp
    impl/core/attribute/binary_test.cpp
    impl/core/attribute/cow_test.cpp
    impl/core/attribute/memory_test.cpp
    impl/core/attribute/merge_test.cpp
    impl/core/attribute/refcount_test.cpp
    impl/core/events/enum_test.cpp
    impl/core/events/omissible_test.cpp
    impl/core/events/struct_test.cpp
    impl/core/events/timestamp_test.cpp
    impl/core/feature_types/rum_test.cpp
    impl/core/json/primitives/bool_test.cpp
    impl/core/json/primitives/float_test.cpp
    impl/core/json/primitives/integer_test.cpp
    impl/core/json/primitives/null_test.cpp
    impl/core/json/primitives/string_test.cpp
    impl/core/json/primitives/timestamp_test.cpp
    impl/core/json/primitives/uuid_test.cpp
    impl/core/json/attribute_test.cpp
    impl/core/json/optional_test.cpp
    impl/core/platform/clock_test.cpp
    impl/core/platform/http_test.cpp
    impl/core/platform/system_info_test.cpp
    impl/core/storage/artifact_test.cpp
    impl/core/storage/feature_event_test.cpp
    impl/core/storage/filesystem_test.cpp
    impl/core/storage/filesystem_wrapper_test.cpp
    impl/core/storage/path_test.cpp
    impl/core/storage/sdk_test.cpp
    impl/core/util/diagnostics_test.cpp
    impl/core/util/error_message_test.cpp
    impl/core/util/json_test.cpp
    impl/core/util/validation_test.cpp
    impl/core/block_test.cpp
    impl/core/context_test.cpp
    impl/core/context_thread_test.cpp
    impl/core/message_bus_test.cpp
    impl/core/messaging_thread_test.cpp
    impl/core/core_test.cpp
    impl/core/feature_id_test.cpp
    impl/core/feature_read_test.cpp
    impl/core/feature_scope_test.cpp
    impl/core/feature_test.cpp
    impl/core/queue_test.cpp
    impl/core/request_builder_test.cpp
    impl/core/storage_queue_test.cpp
    impl/core/storage_thread_test.cpp
    impl/core/storage_write_test.cpp
    impl/core/tlv_test.cpp
    impl/core/upload_scheduler_test.cpp
    impl/core/upload_thread_test.cpp
    impl/core/upload_util_test.cpp
    impl/core/writer_test.cpp
    impl/crash_reporting/data/crash_context_read_test.cpp
    impl/crash_reporting/data/crash_report_read_test.cpp
    impl/crash_reporting/crash_processing_test.cpp
    impl/crash_reporting/crash_reporting_test.cpp
    impl/logging/logger_test.cpp
    impl/rum/containers/resource_map_test.cpp
    impl/rum/containers/view_array_test.cpp
    impl/rum/crash_processing/json_scanner_test.cpp
    impl/rum/scopes/action_test.cpp
    impl/rum/scopes/application_test.cpp
    impl/rum/scopes/resource_test.cpp
    impl/rum/scopes/session_test.cpp
    impl/rum/scopes/view_test.cpp
    impl/rum/command_test.cpp
    impl/rum/context_test.cpp
    impl/rum/rum_messaging_test.cpp
    impl/rum/rum_test.cpp
    impl/rum/scope_test.cpp
    mock/filesystem.cpp
    mock/http_client_mock_test.cpp
    support/allocation_tracker.cpp
    support/attribute_debug.cpp
)

# crash_context_write and crash_report_write are only compiled into dd_native when using
# the in-process crash handler; only include their tests if they're present in the build
get_target_property(_dd_native_sources dd_native SOURCES)
set(_crash_src_dir "datadog/impl/crash_reporting/data")
if("${_crash_src_dir}/crash_context_write.cpp" IN_LIST _dd_native_sources)
    target_sources(dd_native_tests PRIVATE
        impl/crash_reporting/data/crash_context_write_test.cpp
    )
endif()
if("${_crash_src_dir}/crash_report_write.cpp" IN_LIST _dd_native_sources)
    target_sources(dd_native_tests PRIVATE
        impl/crash_reporting/data/crash_report_write_test.cpp
    )
endif()
unset(_dd_native_sources)
unset(_crash_src_dir)

# Add Windows-specific tests
if(WIN32)
    target_sources(dd_native_tests PRIVATE
        impl/core/platform/windows_timezone_mapping_test.cpp
    )
endif()

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
    PRIVATE ${DD_SDK_ROOT_DIR}/src
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

# The vast majority of tests can be run in parallel and require no special configuration
catch_discover_tests(dd_native_tests
    TEST_SPEC "~[writes-to-cwd-datadog]"
)

# A small subset of tests need to write to $(pwd)/.datadog/, then assert on its contents
# before deleting it - for these tests, use a RESOURCE_LOCK to ensure that they're each
# run serially
catch_discover_tests(dd_native_tests
    TEST_SPEC "[writes-to-cwd-datadog]"
    PROPERTIES RESOURCE_LOCK "cwd-datadog"
)
