# Include development-only dependencies
include(${DD_SDK_ROOT_DIR}/cmake/deps/catch2.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/deps/nlohmann-json.cmake)

# Create test executable
add_executable(tests
    c/attribute_c_api_test.cpp
    c/core_c_api_test.cpp
    c/diagnostics_c_api_test.cpp
    c/logging_c_api_test.cpp
    c/rum_c_api_test.cpp
    c/timestamp_c_api_test.cpp
    c/account_info_c_api_test.cpp
    c/user_info_c_api_test.cpp
    c/uuid_c_api_test.cpp
    c/version_c_api_test.cpp
    cpp/attribute_cpp_api_test.cpp
    cpp/core_cpp_api_test.cpp
    cpp/diagnostics_cpp_api_test.cpp
    cpp/logging_cpp_api_test.cpp
    cpp/rum_cpp_api_test.cpp
    cpp/timestamp_cpp_api_test.cpp
    cpp/account_info_cpp_api_test.cpp
    cpp/user_info_cpp_api_test.cpp
    cpp/uuid_cpp_api_test.cpp
    cpp/version_cpp_api_test.cpp
    impl/types/attribute/cow_test.cpp
    impl/types/attribute/memory_test.cpp
    impl/types/attribute/merge_test.cpp
    impl/types/attribute/refcount_test.cpp
    impl/types/json/primitives/bool_test.cpp
    impl/types/json/primitives/float_test.cpp
    impl/types/json/primitives/integer_test.cpp
    impl/types/json/primitives/null_test.cpp
    impl/types/json/primitives/string_test.cpp
    impl/types/json/primitives/timestamp_test.cpp
    impl/types/json/primitives/uuid_test.cpp
    impl/types/json/attribute_test.cpp
    impl/types/json/optional_test.cpp
    impl/types/json/parse_attribute_test.cpp
    impl/types/json/parse_primitives_test.cpp
    impl/types/events/enum_test.cpp
    impl/types/events/omissible_test.cpp
    impl/types/events/struct_test.cpp
    impl/types/events/timestamp_test.cpp
    impl/types/diagnostics_test.cpp
    impl/types/json/json_scanner_test.cpp
    impl/types/json_test.cpp
    impl/types/rum_test.cpp
    impl/types/sampling_test.cpp
    impl/types/upload_util_test.cpp
    impl/crash_processing/crash_context_annotations_test.cpp
    impl/crash_processing/crash_events_test.cpp
    impl/crash_processing/crash_formatting_test.cpp
    impl/crash_processing/view_event_mutation_test.cpp
    impl/crash_processing/view_event_parser_test.cpp
    impl/core/attribute/binary_test.cpp
    impl/core/http/body_writer_string_test.cpp
    impl/core/http/body_writer_tlv_test.cpp
    impl/core/http/client_test.cpp
    impl/core/http/request_builder_test.cpp
    impl/core/platform/clock_test.cpp
    impl/core/platform/system_info_test.cpp
    impl/core/storage/artifact_test.cpp
    impl/core/storage/feature_event_test.cpp
    impl/core/storage/filesystem_test.cpp
    impl/core/storage/filesystem_wrapper_test.cpp
    impl/core/storage/path_test.cpp
    impl/core/storage/sdk_test.cpp
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
    impl/core/storage_queue_test.cpp
    impl/core/storage_thread_test.cpp
    impl/core/storage_write_test.cpp
    impl/core/tlv_test.cpp
    impl/core/upload_scheduler_test.cpp
    impl/core/upload_thread_test.cpp
    impl/crash_reporting/data/crash_context_read_test.cpp
    impl/crash_reporting/data/crash_report_read_test.cpp
    impl/crash_reporting/crash_processing_test.cpp
    impl/crash_reporting/crash_reporting_test.cpp
    impl/logging/event_generation_test.cpp
    impl/logging/logger_test.cpp
    impl/logging/tags_test.cpp
    impl/rum/containers/resource_map_test.cpp
    impl/rum/containers/view_array_test.cpp
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

# crash_context_write and crash_report_write are only compiled into sdk when using
# the in-process crash handler; only include their tests if they're present in the build
get_target_property(_sdk_sources ddsdkcpp SOURCES)
set(_crash_src_dir "datadog/impl/crash_reporting/data")
if("${_crash_src_dir}/crash_context_write.cpp" IN_LIST _sdk_sources)
    target_sources(tests PRIVATE
        impl/crash_reporting/data/crash_context_write_test.cpp
    )
endif()
if("${_crash_src_dir}/crash_report_write.cpp" IN_LIST _sdk_sources)
    target_sources(tests PRIVATE
        impl/crash_reporting/data/crash_report_write_test.cpp
    )
endif()
unset(_crash_src_dir)
if("datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.cpp" IN_LIST _sdk_sources)
    target_sources(tests PRIVATE
        impl/crash_reporting/view_event_fit_test.cpp
    )
endif()
unset(_sdk_sources)

# Add Windows-specific tests
if(WIN32)
    target_sources(tests PRIVATE
        impl/core/platform/windows_timezone_mapping_test.cpp
    )
endif()

target_include_external_deps(tests)
target_enable_strict_warnings(tests)
target_enable_parallel_compilation(tests)

# Catch2's std::unreachable polyfill uses __assume(false) before std::terminate()
# in NDEBUG builds, which makes MSVC flag the terminate() call as unreachable code
# (C4702). This is a back-end/codegen warning, so /external:W0 can't suppress it and
# /WX promotes it to an error. Disable C4702 for this target to work around it.
if(MSVC)
    target_compile_options(tests PRIVATE /wd4702)
endif()

target_enable_datadog_assert(tests)
target_enable_coverage(tests)
target_enable_sanitizers(tests)

if(DD_ENABLE_TEST_ALLOCATION_TRACKING)
    target_compile_definitions(tests PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=1)
else()
    target_compile_definitions(tests PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=0)
endif()

# Strict checks in memory tests are automatically enabled on our default
# macOS/Clang/libc++ development environment; disabled by default everywhere else
if(DD_ENABLE_STRICT_MEMORY_CHECKS)
    target_compile_definitions(tests PRIVATE WITH_DATADOG_STRICT_MEMORY_CHECKS=1)
endif()

# Asserts in thread-safety tests are disabled by default, as they're inherently
# timing-dependent and can therefore be flaky; but they can be explicitly enabled
if(DD_ENABLE_STRICT_THREADING_CHECKS)
    target_compile_definitions(tests PRIVATE WITH_DATADOG_STRICT_THREADING_CHECKS=1)
endif()

target_include_directories(tests
    PRIVATE ${DD_SDK_ROOT_DIR}/src
    PRIVATE .
)

target_link_libraries(tests
    PRIVATE
    ddsdkcpp
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)

# The vast majority of tests can be run in parallel and require no special configuration
catch_discover_tests(tests
    TEST_SPEC "~[writes-to-cwd-datadog]"
)

# A small subset of tests need to write to $(pwd)/.datadog/, then assert on its contents
# before deleting it - for these tests, use a RESOURCE_LOCK to ensure that they're each
# run serially
catch_discover_tests(tests
    TEST_SPEC "[writes-to-cwd-datadog]"
    PROPERTIES RESOURCE_LOCK "cwd-datadog"
)
