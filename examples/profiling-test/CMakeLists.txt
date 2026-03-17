# Profiling + RUM integration smoke test
# Only built when DD_ENABLE_PROFILER is ON (requires dd-win-prof).

add_executable(dd_profiling_test
    main.cpp
)
target_compile_definitions(dd_profiling_test PRIVATE _CRT_SECURE_NO_WARNINGS)
target_enable_strict_warnings(dd_profiling_test)
target_enable_coverage(dd_profiling_test)
target_enable_sanitizers(dd_profiling_test)
datadog_enable(dd_profiling_test)
