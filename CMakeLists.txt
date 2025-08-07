cmake_minimum_required(VERSION 3.21)
project(dd-native VERSION 0.1.0 LANGUAGES C CXX)

# Run builds with -DDD_DEVELOPMENT=ON to build everything by default
option(DD_DEVELOPMENT "Enable development mode (examples, tests, analysis)" OFF)

# Library implementation uses C++17; C API expects C99
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Explicitly enable strict warnings on a per-target basis
function(target_enable_strict_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# If configured to use clang-tidy, find it in the PATH
option(DD_ENABLE_CLANG_TIDY "Enable clang-tidy" ${DD_DEVELOPMENT})
if(DD_ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_BINARY NAMES "clang-tidy")
    if(CLANG_TIDY_BINARY)
        message(STATUS "clang-tidy found: ${CLANG_TIDY_BINARY}")
    else()
        message(FATAL_ERROR "clang-tidy not found! Install clang-tidy or set DD_ENABLE_CLANG_TIDY=OFF")
    endif()
endif()

# Explicitly enable clang-tidy on a per-target basis
function(target_enable_clang_tidy target_name)
    if(DD_ENABLE_CLANG_TIDY AND CLANG_TIDY_BINARY)
        set_target_properties(${target_name} PROPERTIES
            CXX_CLANG_TIDY "${CLANG_TIDY_BINARY}"
            C_CLANG_TIDY "${CLANG_TIDY_BINARY}"
        )
    endif()
endfunction()

# Core implementation library (private)
add_subdirectory(src/impl)

# Public API libraries
add_subdirectory(src/c)
add_subdirectory(src/cpp)

# If configured to build examples, include examples/ in the build
option(DD_BUILD_EXAMPLES "Build examples" ${DD_DEVELOPMENT})
if(DD_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# If configured to build tests, include tests/ and set up ctest
option(DD_BUILD_TESTING "Build tests" ${DD_DEVELOPMENT})
if(DD_BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
