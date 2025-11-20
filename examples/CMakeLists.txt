# C example
add_executable(dd_native_c_example
    c/main.c
)
target_enable_strict_warnings(dd_native_c_example)
target_enable_coverage(dd_native_c_example)
target_enable_sanitizers(dd_native_c_example)

target_link_libraries(dd_native_c_example PRIVATE dd_native)

# C++ example
add_executable(dd_native_cpp_example
    cpp/main.cpp
)
target_enable_strict_warnings(dd_native_cpp_example)
target_enable_coverage(dd_native_cpp_example)
target_enable_sanitizers(dd_native_cpp_example)

target_link_libraries(dd_native_cpp_example PRIVATE dd_native)

# REPL
add_executable(dd_native_repl
    repl/main.cpp
    repl/args.cpp
    repl/buffer.cpp
    repl/command.cpp
    repl/commands_config.cpp
    repl/commands_core.cpp
    repl/commands_logging.cpp
    repl/commands_rum.cpp
    repl/commands_util.cpp
)
target_enable_strict_warnings(dd_native_repl)
target_enable_clang_tidy(dd_native_repl)
target_enable_coverage(dd_native_repl)
target_enable_sanitizers(dd_native_repl)
target_link_libraries(dd_native_repl PRIVATE dd_native)
target_include_directories(dd_native_repl PRIVATE .)
