# profiling: the Profiling feature, which runs the dd-win-prof profiler (Windows x64
# only; see DD_ENABLE_PROFILING) and tracks the RUM activity that occurs while it runs.
dd_add_module(profiling
    DEPENDS core
    PUBLIC_HEADERS profiling
    SOURCES
        datadog/c/profiling_c_api.cpp
        datadog/cpp/profiling_cpp_api.cpp
        datadog/impl/profiling/profile_metadata.cpp
        datadog/impl/profiling/profiling.cpp
        datadog/impl/profiling/rum_event_tracker.cpp
)

# If building with profiling support, build dd-win-prof and link it into this module;
# otherwise, compile the CreateProfilerBackend() variant that reports profiling as unsupported
if(DD_PROFILER_IMPL STREQUAL "dd-win-prof")
    include(${DD_SDK_ROOT_DIR}/cmake/deps/dd-win-prof.cmake)
else()
    target_sources(ddsdkcpp_profiling PRIVATE datadog/impl/profiling/profiler_backend_unsupported.cpp)
endif()
