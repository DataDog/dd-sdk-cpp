cmake_minimum_required(VERSION 3.21)

# Default to targeting macOS v15: this must appear before project() as it affects
# toolchain selection
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
endif()

project(ddsdkcpp VERSION 0.4.0 LANGUAGES C CXX)

# If we're the top-level project, we're being built in the original source repo, where
# we want to generate installable builds, etc; if not, we're being pulled in as a
# third-party dependency via FetchContent
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(DD_IS_TOP_LEVEL ON)
else()
    set(DD_IS_TOP_LEVEL OFF)
endif()

# Run builds with -DDD_DEVELOPMENT=ON to enable all required options for internal
# development of the SDK implementation
option(DD_DEVELOPMENT "Enable development mode (examples, tests, analysis, asserts)" OFF)
option(DD_DEVELOPMENT_ALLOW_AUTO_INSTALL "If enabled, required development tools (e.g. clang-format) may be automatically downloaded and installed to llvm-tools/" OFF)

# If building the SDK as the top-level project and not using a multi-config generator,
# ensure that CMAKE_BUILD_TYPE is set to a reasonable default. Note that when the SDK is
# included in another project as a dependency, that project owns CMAKE_BUILD_TYPE and
# must set it explicitly.
if(DD_IS_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    # Use debug if running a local development build to actively work on the SDK;
    # otherwise produce an optimized release build with symbols.
    if(DD_DEVELOPMENT)
        set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type")
    else()
        set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type")
    endif()
endif()

# Top-level options for the build
option(DD_BUILD_SHARED "Build Datadog SDK as shared library" ${BUILD_SHARED_LIBS})
set(DD_CRASH_MODE "inprocess" CACHE STRING "Crash handler mode: noop, inprocess, or crashpad")
set_property(CACHE DD_CRASH_MODE PROPERTY STRINGS "noop" "inprocess" "crashpad")
option(DD_BUILD_EXAMPLES "Build examples" ${DD_DEVELOPMENT})
option(DD_BUILD_TESTING "Build tests" ${DD_DEVELOPMENT})
option(DD_BUILD_TOOLS "Build optional utilities from C++ source within tools/" ${DD_DEVELOPMENT})
option(DD_BUILD_INSTALL "Enable install targets" ${DD_IS_TOP_LEVEL})
if(DD_BUILD_INSTALL)
    option(DD_BUILD_PACKAGE "Enable package targets and build required tools" ${DD_BUILD_INSTALL})
endif()
option(DD_ENABLE_GIT_REVISION_INFO "Stamp the build with the git branch name, commit sha, and blob hash of src/ (incl. pending changes) computed at configure-time" ${DD_BUILD_INSTALL})
option(DD_ENABLE_EVENT_VALIDATION "Include targets for validating JSON event payloads against rum-events-format schemas (requires Python)" ${DD_DEVELOPMENT})
option(DD_ENABLE_INTEGRATION_TEST "Include targets for running integration tests (requires Python and DD_BUILD_EXAMPLES)" "${DD_DEVELOPMENT}")
option(DD_ENABLE_CLANG_FORMAT "Enable clang-format for format validation and automatic formatting" ${DD_DEVELOPMENT})
option(DD_ENABLE_CLANG_TIDY "Enable clang-tidy for static analysis" ${DD_DEVELOPMENT})
option(DD_ENABLE_COVERAGE "Enable code coverage reporting" ${DD_DEVELOPMENT})
if(DD_DEVELOPMENT AND NOT DEFINED DD_ENABLE_SANITIZERS)
    set(DD_ENABLE_SANITIZERS "ASan,UBSan")
endif()
set(DD_ENABLE_SANITIZERS "${DD_ENABLE_SANITIZERS}" CACHE STRING "Comma-separated list of sanitizers to enable, e.g. 'ASan,UBSan', 'MSan,UBSan', 'TSan'")
option(DD_ENABLE_ASSERTS "Enable assertions in Datadog SDK implementation" ${DD_DEVELOPMENT})
option(DD_ENABLE_TEST_ALLOCATION_TRACKING "Enable allocation tracking (via instrumented new/delete) in test binaries" ON)

# C API targets C99; C++ API targets C++17
if(DD_IS_TOP_LEVEL)
    set(CMAKE_C_STANDARD 99)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

    # Crashpad requires C++20, so building the SDK with crashpad support requires
    # targeting C++20
    if(DD_CRASH_MODE STREQUAL "crashpad")
        set(CMAKE_CXX_STANDARD 20)
    endif()
endif()

# Set the root directory for the project: this is build/ when building the library;
# build/_deps/datadog-build (or something similar) when included in another project via
# FetchContent
set(DD_SDK_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Install opt-in convenience helpers, allowing typical applications to just call
# datadog_enable(primary_executable_target) in their CMake builds
include("${DD_SDK_ROOT_DIR}/cmake/DatadogConvenience.cmake")
if(DD_BUILD_INSTALL)
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DatadogConvenience.cmake"
        DESTINATION lib/cmake/Datadog
    )
endif()

# Modules for project build configuration
include(${DD_SDK_ROOT_DIR}/cmake/llvm-tools.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/clang-format.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/clang-tidy.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/compile.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/coverage.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/sanitizers.cmake)
include(${DD_SDK_ROOT_DIR}/cmake/external.cmake)

# If we want to stamp the build git revision info, compute those details and save them
# to variables at configure time
if(DD_ENABLE_GIT_REVISION_INFO)
    include(${DD_SDK_ROOT_DIR}/cmake/git-revision-info.cmake)
endif()

# If JSON event validation is enabled, require Python and add targets
if(DD_ENABLE_EVENT_VALIDATION)
    include(${DD_SDK_ROOT_DIR}/cmake/event-validation.cmake)
endif()

# If integration tests are enabled, require Python and add targets
if(DD_ENABLE_INTEGRATION_TEST)
    include(${DD_SDK_ROOT_DIR}/cmake/integration-test.cmake)
endif()

# Include the library itself
add_subdirectory(src)
add_library(Datadog::sdk ALIAS ddsdkcpp)

# If configured to build examples, include examples/ in the build
if(DD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# If configured to build tests, include tests/ and set up ctest
if(DD_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# 'check-all' is a convenience target that mirrors what CI runs: formatting,
# a full test build (which triggers clang-tidy via the CXX_CLANG_TIDY target
# property), and the test suite. Agents and contributors should prefer this
# single target over invoking each check individually.
#
# Gated on DD_IS_TOP_LEVEL so the generic 'check-all' name does not leak into
# parent projects that consume this SDK via FetchContent or add_subdirectory.
if(DD_IS_TOP_LEVEL AND TARGET check-format AND DD_BUILD_TESTING)
    add_custom_target(check-all
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
        COMMENT "Running local CI checks: clang-format, build (with clang-tidy), and tests"
        VERBATIM
    )
    # Building the `tests` target forces a full compile of the SDK and test suite,
    # which is where clang-tidy runs via CXX_CLANG_TIDY / C_CLANG_TIDY.
    add_dependencies(check-all check-format tests)
endif()

# If configured to build developer tools, include the CMake targets defined in tools/
# for utilities written in C++
if(DD_BUILD_TOOLS)
    add_subdirectory(tools/print-system-info)
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
