# Define a simple utility program that will print system info
add_executable(dd_system_info main.cpp)

# Write binary to build/dd_system_info
set_target_properties(dd_system_info PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Compile with the same build settings as the main SDK library, for linker compatibility
target_enable_strict_warnings(dd_system_info)
target_enable_coverage(dd_system_info)
target_enable_sanitizers(dd_system_info)

# Link against dd-native to get SystemInfo implementation and code required for
# diagnostic logging etc.
target_link_libraries(dd_system_info PRIVATE Datadog::dd_native)

# The SystemInfo interface is not publicly exposed: include the internal source of the
# SDK so we can call SystemInfo functions directly
target_include_directories(dd_system_info PRIVATE ${DD_SDK_ROOT_DIR}/src)
