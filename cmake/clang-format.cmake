# Must be included after llvm-tools.cmake
#
# Defines two targets:
#   * format       - rewrites all SDK source files in-place using clang-format
#   * check-format - verifies formatting without modifying files (what CI runs)
#
# When clang-format support is disabled or unavailable, placeholder versions of
# these targets are defined instead. Building them fails with an actionable
# message, so users and CI systems never see a cryptic
# "No rule to make target 'format'".

if(DD_ENABLE_CLANG_FORMAT AND CLANG_FORMAT_BINARY)
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
else()
    # Placeholder targets so that `cmake --build build --target format` fails
    # with an actionable message instead of `No rule to make target 'format'`.
    set(_DD_FORMAT_DISABLED_REASON "")
    if(NOT DD_ENABLE_CLANG_FORMAT)
        set(_DD_FORMAT_DISABLED_REASON "DD_ENABLE_CLANG_FORMAT=OFF")
    else()
        set(_DD_FORMAT_DISABLED_REASON "clang-format ${LLVM_TOOLS_VERSION} was not resolved")
    endif()

    add_custom_target(format
        COMMAND ${CMAKE_COMMAND} -E echo " "
        COMMAND ${CMAKE_COMMAND} -E echo "ERROR: the format target is not available in this build."
        COMMAND ${CMAKE_COMMAND} -E echo "Reason: ${_DD_FORMAT_DISABLED_REASON}"
        COMMAND ${CMAKE_COMMAND} -E echo " "
        COMMAND ${CMAKE_COMMAND} -E echo "Reconfigure with clang-format enabled:"
        COMMAND ${CMAKE_COMMAND} -E echo "  cmake -DDD_DEVELOPMENT=ON -DDD_ENABLE_CLANG_FORMAT=ON -DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON -S . -B build"
        COMMAND ${CMAKE_COMMAND} -E echo " "
        COMMAND ${CMAKE_COMMAND} -E echo "See CONTRIBUTING.md -- section: Formatting and linting your code."
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "clang-format support is not enabled in this build"
    )

    add_custom_target(check-format
        COMMAND ${CMAKE_COMMAND} -E echo " "
        COMMAND ${CMAKE_COMMAND} -E echo "ERROR: the check-format target is not available in this build."
        COMMAND ${CMAKE_COMMAND} -E echo "Reason: ${_DD_FORMAT_DISABLED_REASON}"
        COMMAND ${CMAKE_COMMAND} -E echo " "
        COMMAND ${CMAKE_COMMAND} -E echo "Reconfigure with clang-format enabled:"
        COMMAND ${CMAKE_COMMAND} -E echo "  cmake -DDD_DEVELOPMENT=ON -DDD_ENABLE_CLANG_FORMAT=ON -DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON -S . -B build"
        COMMAND ${CMAKE_COMMAND} -E echo " "
        COMMAND ${CMAKE_COMMAND} -E echo "See CONTRIBUTING.md -- section: Formatting and linting your code."
        COMMAND ${CMAKE_COMMAND} -E false
        COMMENT "clang-format support is not enabled in this build"
    )

    unset(_DD_FORMAT_DISABLED_REASON)
endif()
