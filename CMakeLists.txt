cmake_minimum_required(VERSION 3.21)
project(ddsdkcpp VERSION 0.2.0 LANGUAGES C CXX)

# If we're the top-level project, we're being built in the original source repo, where
# we want to generate installable builds, etc; if not, we're being pulled in as a
# third-party dependency via FetchContent
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(DD_IS_TOP_LEVEL ON)
else()
    set(DD_IS_TOP_LEVEL OFF)
endif()

# Library implementation uses C++17; C API expects C99
if(DD_IS_TOP_LEVEL)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Run builds with -DDD_DEVELOPMENT=ON to enable all required options for internal
# development of the SDK implementation
option(DD_DEVELOPMENT "Enable development mode (examples, tests, analysis, asserts)" OFF)
option(DD_DEVELOPMENT_ALLOW_AUTO_INSTALL "If enabled, required development tools (e.g. clang-format) may be automatically downloaded and installed to toos/bin/" OFF)

# Top-level options for the build
option(DD_BUILD_SHARED "Build Datadog SDK as shared library" ${BUILD_SHARED_LIBS})
option(DD_BUILD_BENCHMARKS "Build benchmarks" ${DD_DEVELOPMENT})
option(DD_BUILD_EXAMPLES "Build examples" ${DD_DEVELOPMENT})
option(DD_BUILD_TESTING "Build tests" ${DD_DEVELOPMENT})
option(DD_BUILD_INSTALL "Enable install targets" ${DD_IS_TOP_LEVEL})
if(DD_BUILD_INSTALL)
    option(DD_BUILD_PACKAGE "Enable package targets and build required tools" ${DD_BUILD_INSTALL})
endif()
option(DD_ENABLE_CLANG_FORMAT "Enable clang-format for format validation and automatic formatting" ${DD_DEVELOPMENT})
option(DD_ENABLE_CLANG_TIDY "Enable clang-tidy for static analysis" ${DD_DEVELOPMENT})
option(DD_ENABLE_COVERAGE "Enable code coverage reporting" ${DD_DEVELOPMENT})
if(DD_DEVELOPMENT AND NOT DEFINED DD_ENABLE_SANITIZERS)
    set(DD_ENABLE_SANITIZERS "ASan,UBSan")
endif()
set(DD_ENABLE_SANITIZERS "${DD_ENABLE_SANITIZERS}" CACHE STRING "Comma-separated list of sanitizers to enable, e.g. 'ASan,UBSan', 'MSan,UBSan', 'TSan'")
option(DD_ENABLE_ASSERTS "Enable assertions in Datadog SDK implementation" ${DD_DEVELOPMENT})
option(DD_ENABLE_TEST_ALLOCATION_TRACKING "Enable allocation tracking (via instrumented new/delete) in test binaries" ON)

# Set the root directory for the project: this is build/ when building the library;
# build/_deps/datadog-build (or something similar) when included in another project via
# FetchContent
set(DD_SDK_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Modules for project build configuration
include(${DD_SDK_ROOT_DIR}/cmake/clang-format.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/clang-tidy.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/compile.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/coverage.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/sanitizers.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/external.cmake)

# Include the library itself
add_subdirectory(src)
add_library(Datadog::dd_native ALIAS dd_native)

# If configured to build benchmarks, include benchmarks/ in the build
if(DD_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# If configured to build examples, include examples/ in the build
if(DD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# If configured to build tests, include tests/ and set up ctest
if(DD_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Only install when DD_BUILD_INSTALL is enabled
if(DD_BUILD_INSTALL)
    include(GNUInstallDirs)

    # Install headers
    install(DIRECTORY include-c/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h"
    )

    install(DIRECTORY include-cpp/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
    )

    # Install export targets and generate CMake config files
    install(EXPORT DatadogTargets
        FILE DatadogTargets.cmake
        NAMESPACE Datadog::
        DESTINATION lib/cmake/Datadog
    )

    include(CMakePackageConfigHelpers)
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/DatadogConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY AnyNewerVersion
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DatadogConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/DatadogConfig.cmake"
        INSTALL_DESTINATION lib/cmake/Datadog
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/DatadogConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/DatadogConfigVersion.cmake"
        DESTINATION lib/cmake/Datadog
    )
endif()

# If including package commands, build the required utilities and define package targets
if (DD_BUILD_PACKAGE)
    include(${DD_SDK_ROOT_DIR}/cmake/package.cmake)
endif()
