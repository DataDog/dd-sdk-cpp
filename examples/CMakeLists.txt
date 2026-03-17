# C example
add_executable(dd_native_c_example
    c/main.c
)
target_enable_strict_warnings(dd_native_c_example)
target_enable_coverage(dd_native_c_example)
target_enable_sanitizers(dd_native_c_example)
datadog_enable(dd_native_c_example)

# C++ example
add_executable(dd_native_cpp_example
    cpp/main.cpp
)
target_enable_strict_warnings(dd_native_cpp_example)
target_enable_coverage(dd_native_cpp_example)
target_enable_sanitizers(dd_native_cpp_example)
datadog_enable(dd_native_cpp_example)

# REPL
add_executable(dd_native_repl
    repl/main.cpp
    repl/alloc.cpp
    repl/args.cpp
    repl/buffer.cpp
    repl/command.cpp
    repl/commands_config.cpp
    repl/commands_core.cpp
    repl/commands_crash.cpp
    repl/commands_logging.cpp
    repl/commands_profile.cpp
    repl/commands_rum.cpp
    repl/commands_util.cpp
)
target_compile_definitions(dd_native_repl PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=1)
target_enable_strict_warnings(dd_native_repl)
target_enable_clang_tidy(dd_native_repl)
target_enable_coverage(dd_native_repl)
target_enable_sanitizers(dd_native_repl)
target_include_directories(dd_native_repl PRIVATE .)
datadog_enable(dd_native_repl)

# Profiling + RUM integration smoke test (requires dd-win-prof)
if(DD_ENABLE_PROFILER)
    add_subdirectory(profiling-test)
endif()
