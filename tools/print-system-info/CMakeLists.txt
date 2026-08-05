# Define a simple utility program that will print system info
add_executable(system_info main.cpp)

# Write binary to build/system_info
set_target_properties(system_info PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Compile with the same build settings as the main SDK library, for linker compatibility
target_enable_strict_warnings(system_info)
target_enable_parallel_compilation(system_info)
target_enable_coverage(system_info)
target_enable_sanitizers(system_info)

# Link against `Datadog::sdk` to get SystemInfo implementation and code required for
# diagnostic logging etc.
target_link_libraries(system_info PRIVATE Datadog::sdk)

# The SystemInfo interface is not publicly exposed: include the internal source of the
# SDK so we can call SystemInfo functions directly
target_include_directories(system_info PRIVATE ${DD_SDK_ROOT_DIR}/src)
