# Must be included after llvm-tools.cmake

# If we're building with clang-tidy support, ensure that we resolved a supported
# clang-tidy binary
if(DD_ENABLE_CLANG_TIDY)
    if(NOT CLANG_TIDY_BINARY)
        message(FATAL_ERROR "clang-tidy not found! Install clang-tidy or set DD_ENABLE_CLANG_TIDY=OFF")
    endif()

    # TEMP: In an attempt to understand why clang-tidy processes are being killed in CI,
    # invoke clang-tidy via a shim script, debug-clang-tidy.sh, which will print more
    # diagnostic information while wrapping the actual clang-tidy binary
    if(DD_ENABLE_CI_DIAGNOSTICS)
        file(WRITE "${CMAKE_BINARY_DIR}/clang-tidy-path" "${CLANG_TIDY_BINARY}")
        set(CLANG_TIDY_BINARY "${DD_SDK_ROOT_DIR}/debug-clang-tidy.sh")
    endif()
endif()

# Explicitly enable clang-tidy on a per-target basis
function(target_enable_clang_tidy target_name)
    if(DD_ENABLE_CLANG_TIDY AND CLANG_TIDY_BINARY)
        set_target_properties(${target_name} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_BINARY}"
            C_CLANG_TIDY "${CLANG_TIDY_BINARY}"
        )
    endif()
endfunction()
