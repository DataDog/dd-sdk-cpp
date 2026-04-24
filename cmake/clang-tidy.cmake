# Must be included after llvm-tools.cmake

# If we're building with clang-tidy support, ensure that we resolved a supported
# clang-tidy binary
if(DD_ENABLE_CLANG_TIDY)
    if(NOT CLANG_TIDY_BINARY)
        message(FATAL_ERROR "clang-tidy not found! Install clang-tidy or set DD_ENABLE_CLANG_TIDY=OFF")
    endif()

    # clang-tidy needs a resource directory to find compiler built-in headers
    # (stdarg.h, stddef.h, etc.) that the macOS SDK and other system headers
    # pull in. A full LLVM installation has these at ../lib/clang/<ver>/include/
    # relative to the binary, but the auto-installed binary in llvm-bin/ is a
    # stripped standalone binary with no such sibling directory. Point clang-tidy
    # at the resource directory of the actual compiler so it can always find
    # the built-ins, regardless of whether the binary is a full install or not.
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE DD_CLANG_RESOURCE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(DD_CLANG_RESOURCE_DIR)
        message(STATUS "clang-tidy resource dir: ${DD_CLANG_RESOURCE_DIR}")
    else()
        message(WARNING "Could not determine compiler resource dir; clang-tidy may fail to find built-in headers")
    endif()
endif()

# Explicitly enable clang-tidy on a per-target basis
function(target_enable_clang_tidy target_name)
    if(DD_ENABLE_CLANG_TIDY AND CLANG_TIDY_BINARY)
        set(DD_CLANG_TIDY_COMMAND "${CLANG_TIDY_BINARY}")
        if(DD_CLANG_RESOURCE_DIR)
            list(APPEND DD_CLANG_TIDY_COMMAND "--extra-arg=-resource-dir=${DD_CLANG_RESOURCE_DIR}")
        endif()
        set_target_properties(${target_name} PROPERTIES
            CXX_CLANG_TIDY "${DD_CLANG_TIDY_COMMAND}"
            C_CLANG_TIDY "${DD_CLANG_TIDY_COMMAND}"
        )
    endif()
endfunction()
