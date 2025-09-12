# Build a command-line utility that we can run to dump out the artifact name and version
# information stamped into the library build
add_executable(dd_native_version ${DD_SDK_ROOT_DIR}/src/version/main.cpp)
target_enable_strict_warnings(dd_native_version)
target_enable_coverage(dd_native_version)
target_enable_sanitizers(dd_native_version)
target_link_libraries(dd_native_version PRIVATE dd_native)
target_include_directories(dd_native_version PRIVATE ${DD_SDK_ROOT_DIR}/src/impl)

# When that target is finished building, run `dd_native_version artifact` to write the
# artifact name (e.g. 'ddsdkcpp-v0.2.0-macos-arm64-clang-libc++-static-release') to a
# file in our build directory: our custom CPackConfig.cmake.in template will read from
# this file and set CPACK_PACKAGE_FILE_NAME
add_custom_command(TARGET dd_native_version POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Writing build artifact name to artifact_name.txt"
    COMMAND $<TARGET_FILE:dd_native_version> -e artifact > ${CMAKE_CURRENT_BINARY_DIR}/artifact_name.txt
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
