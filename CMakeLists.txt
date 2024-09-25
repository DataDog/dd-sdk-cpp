cmake_minimum_required(VERSION 3.21)

project(dd-sdk-cpp VERSION 0.1.0)

# Add a custom CMake Modules directory
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/modules
                      ${CMAKE_MODULE_PATH})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_CLANG_TIDY clang-tidy)

enable_testing()

option(DD_SDK_BUILD_TEST "Build the unit tests (packages/*/test/)" ON)

add_subdirectory(packages/core)

set(CLANG_FORMAT_BIN_NAME clang-format)
set(CLANG_FORMAT_EXCLUDE_PATTERNS  "build/" "deps/" ${CMAKE_BINARY_DIR})
find_package(ClangFormat)
