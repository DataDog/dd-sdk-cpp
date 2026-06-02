# Build the `version` utility, which dumps the artifact name and version information
# stamped into the library build
add_executable(version ${DD_SDK_ROOT_DIR}/src/version/main.cpp)
target_enable_strict_warnings(version)
target_enable_coverage(version)
target_enable_sanitizers(version)
target_link_libraries(version PRIVATE sdk)
target_include_directories(version PRIVATE ${DD_SDK_ROOT_DIR}/src)

# When that target is finished building, run `./version -e artifact` to write the
# artifact name (e.g. 'ddsdkcpp-v0.2.0-macos-arm64-clang-libc++-static-release') to a
# file in our build directory: our custom CPackConfig.cmake.in template will read from
# this file and set CPACK_PACKAGE_FILE_NAME
add_custom_command(TARGET version POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Writing build artifact name to artifact_name.txt"
    COMMAND $<TARGET_FILE:version> -e artifact > ${CMAKE_CURRENT_BINARY_DIR}/artifact_name.txt
)

# Use .zip by default on windows; .tar.gz everywhere else
if(WIN32)
set(CPACK_GENERATOR "ZIP")
else()
set(CPACK_GENERATOR "TGZ")
endif()

# Include CPack to define a package target: running
# `cmake --build build --target package` will build the library and write a
# redistributable archive to build/
include(CPack)
