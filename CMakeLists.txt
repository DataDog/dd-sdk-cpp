cmake_minimum_required(VERSION 3.21)
project(dd-native VERSION 0.1.0 LANGUAGES C CXX)

# Run builds with -DDD_DEVELOPMENT=ON to enable all required options for internal
# development of the SDK implementation
option(DD_DEVELOPMENT "Enable development mode (examples, tests, analysis, asserts)" OFF)

# Library implementation uses C++17; C API expects C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Top-level options for the build
option(DD_BUILD_BENCHMARKS "Build benchmarks" ${DD_DEVELOPMENT})
option(DD_BUILD_EXAMPLES "Build examples" ${DD_DEVELOPMENT})
option(DD_BUILD_TESTING "Build tests" ${DD_DEVELOPMENT})
option(DD_ENABLE_CLANG_TIDY "Enable clang-tidy for static analysis" ${DD_DEVELOPMENT})
option(DD_ENABLE_COVERAGE "Enable code coverage reporting" ${DD_DEVELOPMENT})
option(DD_ENABLE_ASSERTS "Enable assertions in Datadog SDK implementation" ${DD_DEVELOPMENT})
option(DD_ENABLE_TEST_ALLOCATION_TRACKING "Enable allocation tracking (via instrumented new/delete) in test binaries" ON)

# Modules for project build configuration
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
include(clang-tidy)
include(compile)
include(coverage)

# Core implementation library (private)
add_subdirectory(src/impl)

# Public API libraries
add_subdirectory(src/c)
add_subdirectory(src/cpp)

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
