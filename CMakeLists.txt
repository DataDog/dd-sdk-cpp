cmake_minimum_required(VERSION 3.21)
project(dd-native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)

# Core implementation library (private)
add_subdirectory(src/impl)

# Public API libraries
add_subdirectory(src/c)
add_subdirectory(src/cpp)

# Examples (optional)
option(DD_BUILD_EXAMPLES "Build examples" OFF)
if(DD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Tests (optional)
option(DD_BUILD_TESTING "Build tests" OFF)
if(DD_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
