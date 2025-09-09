# C example
add_executable(dd_native_c_example
    c/main.c
)
target_enable_strict_warnings(dd_native_c_example)
target_enable_coverage(dd_native_c_example)
target_enable_sanitizers(dd_native_c_example)

target_link_libraries(dd_native_c_example PRIVATE dd_native_c_api)

# C++ example
add_executable(dd_native_cpp_example
    cpp/main.cpp
)
target_enable_strict_warnings(dd_native_cpp_example)
target_enable_coverage(dd_native_cpp_example)
target_enable_sanitizers(dd_native_cpp_example)

target_link_libraries(dd_native_cpp_example PRIVATE dd_native_cpp_api)
