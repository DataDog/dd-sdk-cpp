# Must be included after llvm-tools.cmake

# If building with clang-format support, add custom targets to handle code formatting
if(DD_ENABLE_CLANG_FORMAT)
    # If we didn't resolve a supported clang-format binary, fail
    if(NOT CLANG_FORMAT_BINARY)
        message(FATAL_ERROR "clang-format not found! Install clang-format or set DD_ENABLE_CLANG_TIDY=OFF")
    endif()

    # Collect all SDK source files that should be formatted
    set(FORMAT_DIRECTORIES examples include-c include-cpp src tests)
    set(FORMAT_EXTENSIONS h c hpp cpp)
    set(FORMAT_SOURCE_FILES "")
    foreach(DIR IN LISTS FORMAT_DIRECTORIES)
        foreach(EXT IN LISTS FORMAT_EXTENSIONS)
            file(GLOB_RECURSE DIR_FILES "${DD_SDK_ROOT_DIR}/${DIR}/*.${EXT}")
            list(APPEND FORMAT_SOURCE_FILES ${DIR_FILES})
        endforeach()
    endforeach()

    # 'format': run in-place formatting of all SDK source files
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_BINARY}" -i ${FORMAT_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    # 'check-format': verify that all source files are properly formatted
    add_custom_target(check-format
        COMMAND "${CLANG_FORMAT_BINARY}" --dry-run --Werror ${FORMAT_SOURCE_FILES}
        COMMENT "Checking source code formatting with clang-format"
        VERBATIM
    )
endif()
