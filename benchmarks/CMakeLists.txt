# Embed a file as binary data accessible via static variables
function(target_embed_file target_name input_file variable_name)
    get_filename_component(input_file_abs "${input_file}" ABSOLUTE)
    get_filename_component(input_name "${input_file}" NAME)
    
    # Generate output header file in build directory
    set(output_file "${CMAKE_CURRENT_BINARY_DIR}/embedded_${variable_name}.h")
    
    # Create the embedding script
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/embed_file.cmake" "
file(READ \"\${INPUT_FILE}\" file_content HEX)
string(REGEX REPLACE \"(..)\" \"0x\\\\1, \" hex_content \"\${file_content}\")
string(REGEX REPLACE \", \$\" \"\" hex_content \"\${hex_content}\")
string(LENGTH \"\${file_content}\" content_length)
math(EXPR byte_length \"\${content_length} / 2\")

configure_file(\"\${TEMPLATE_FILE}\" \"\${OUTPUT_FILE}\" @ONLY)
")
    
    # Create the header template
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/embedded_file.h.in" "
#ifndef EMBEDDED_@variable_name@_H
#define EMBEDDED_@variable_name@_H

#include <stddef.h>

#ifdef __cplusplus
extern \"C\" {
#endif

extern const unsigned char @variable_name@_data[];
extern const size_t @variable_name@_size;

#ifdef __cplusplus
}
#endif

// Definition (inline to avoid multiple definition issues)
const unsigned char @variable_name@_data[] = {
  @hex_content@
};
const size_t @variable_name@_size = @byte_length@;

#endif // EMBEDDED_@variable_name@_H
")
    
    # Add custom command to generate the embedded file
    add_custom_command(
        OUTPUT "${output_file}"
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE="${input_file_abs}"
            -DTEMPLATE_FILE="${CMAKE_CURRENT_BINARY_DIR}/embedded_file.h.in"
            -DOUTPUT_FILE="${output_file}"
            -Dvariable_name="${variable_name}"
            -P "${CMAKE_CURRENT_BINARY_DIR}/embed_file.cmake"
        DEPENDS "${input_file_abs}"
        COMMENT "Embedding ${input_name} as ${variable_name}"
    )
    
    # Add the build directory to include path so the header can be found
    target_include_directories(${target_name} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    
    # Add dependency on the generated header
    add_custom_target(${target_name}_embedded_${variable_name} DEPENDS "${output_file}")
    add_dependencies(${target_name} ${target_name}_embedded_${variable_name})
endfunction()

# benchmark program
add_executable(dd_native_benchmark
    common/arg.cpp
    common/benchmark.cpp
    common/config.cpp
    common/global.cpp
    common/server.cpp
    benchmark_logging.cpp
    benchmark_startup.cpp
    main.cpp
)
target_enable_strict_warnings(dd_native_benchmark)
target_enable_clang_tidy(dd_native_benchmark)
target_link_libraries(dd_native_benchmark PRIVATE dd_native_c_api dd_native_cpp_api)
target_include_directories(dd_native_benchmark PRIVATE .)
target_embed_file(dd_native_benchmark common/server.py server_py)
