# types: the SDK's shared data types and the logic that operates on them (attributes,
# UUIDs, timestamps, JSON serialization, trace identifiers, sampling), with no
# dependency on the SDK's core or on any feature. The Crashpad handler process also
# compiles this module (see handlers/crashpad/datadog.patch in crash_reporting).
dd_add_module(types
    PUBLIC_HEADERS api attribute network_instrumentation timestamp uuid version
    SOURCES
        datadog/c/attribute_c_api.cpp
        datadog/c/network_instrumentation_c_api.cpp
        datadog/c/timestamp_c_api.cpp
        datadog/c/uuid_c_api.cpp
        datadog/c/version_c_api.cpp
        datadog/cpp/attribute_cpp_api.cpp
        datadog/cpp/network_instrumentation_cpp_api.cpp
        datadog/cpp/uuid_cpp_api.cpp
        datadog/cpp/version_cpp_api.cpp
        datadog/impl/types/attribute/cow.cpp
        datadog/impl/types/attribute/merge.cpp
        datadog/impl/types/json/primitives/bool.cpp
        datadog/impl/types/json/primitives/float.cpp
        datadog/impl/types/json/primitives/integer.cpp
        datadog/impl/types/json/primitives/null.cpp
        datadog/impl/types/json/primitives/string.cpp
        datadog/impl/types/json/primitives/timestamp.cpp
        datadog/impl/types/json/primitives/uuid.cpp
        datadog/impl/types/json/attribute.cpp
        datadog/impl/types/json/diagnostic_attribute.cpp
        datadog/impl/types/json/json_scanner.cpp
        datadog/impl/types/json/parse_attribute.cpp
        datadog/impl/types/json/parse_primitives.cpp
        datadog/impl/types/diagnostics.cpp
        datadog/impl/types/tracing/deterministic_sampler.cpp
        datadog/impl/types/tracing/sampling_decision_provider.cpp
        datadog/impl/types/tracing/span_id.cpp
        datadog/impl/types/tracing/trace_context.cpp
        datadog/impl/types/tracing/trace_id.cpp
        datadog/impl/types/upload_util.cpp
)

# The public headers are shared by every module: since every other module depends on
# types, they reach every module through this target. See src/CMakeLists.txt for the
# include paths and definitions involved.
target_include_directories(ddsdkcpp_types PUBLIC ${DD_SDK_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(ddsdkcpp_types PUBLIC ${DD_SDK_PUBLIC_DEFINITIONS})

# Platform-specific support for UUID generation
option(DD_UUID_USE_DEFAULT "Use default UUID implementation. If disabled, you must supply an implementation of platform/uuid.hpp in a file specified as DD_UUID_SRC" ON)
if(DD_UUID_USE_DEFAULT)
    # Include every implementation: each one is guarded by preprocessor checks for its
    # platform
    target_sources(ddsdkcpp_types PRIVATE
        datadog/impl/types/platform/uuid_linux.cpp
        datadog/impl/types/platform/uuid_macos.cpp
        datadog/impl/types/platform/uuid_windows.cpp
    )
else()
    set(DD_UUID_SRC "" CACHE STRING "Path to custom implementation of platform/uuid.hpp")
    if(DD_UUID_SRC)
        target_sources(ddsdkcpp_types PRIVATE ${DD_UUID_SRC})
    endif()
endif()
