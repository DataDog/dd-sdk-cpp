# C example
add_executable(c_example
    c/main.c
)
target_enable_strict_warnings(c_example)
target_enable_coverage(c_example)
target_enable_sanitizers(c_example)
datadog_enable(c_example)

# C++ example
add_executable(cpp_example
    cpp/main.cpp
)
target_enable_strict_warnings(cpp_example)
target_enable_coverage(cpp_example)
target_enable_sanitizers(cpp_example)
datadog_enable(cpp_example)

# REPL
add_executable(repl
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
target_compile_definitions(repl PRIVATE WITH_DATADOG_ALLOCATION_TRACKING=1)
target_enable_strict_warnings(repl)
target_enable_clang_tidy(repl)
target_enable_coverage(repl)
target_enable_sanitizers(repl)
target_include_directories(repl PRIVATE .)
datadog_enable(repl)
