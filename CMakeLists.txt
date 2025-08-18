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

# Enable code coverage flags on a per-target basis
option(DD_ENABLE_COVERAGE "Enable code coverage reporting" OFF)
function(target_enable_coverage target_name)
    if(DD_ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang$")
            target_compile_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping -O0)
            target_link_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        else()
            message(FATAL_ERROR "Code coverage is only supported with Clang; configure with DD_ENABLE_COVERAGE=OFF or use Clang")
        endif()
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
    
    # Add custom targets for code coverage if enabled
    if(DD_ENABLE_COVERAGE)
        # When coverage is enabled for a given target (via target_enable_coverage), the
        # Clang compiler injects instrumentation code into the binaries that writes
        # execution data to .profraw files at runtime. llvm-profdata merges these raw
        # profile files into an indexed .profdata file, and llvm-cov can generate
        # coverage reports from the .profdata file and the original binaries.
        find_program(LLVM_PROFDATA_BINARY llvm-profdata)
        find_program(LLVM_COV_BINARY llvm-cov)

        if(LLVM_PROFDATA_BINARY AND LLVM_COV_BINARY)
            # Ensure that our build/coverage/ directory is completely empty: this is our
            # working directory for coverage-related artifacts written by llvm-cov etc.
            add_custom_target(coverage-clean
                COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/coverage
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
                COMMENT "Cleaning coverage data"
            )
            
            # Running `cmake --build <build-dir> --target coverage` will:
            # - Set LLVM_PROFILE_FILE to write .profraw files to coverage/ directory
            # - Invoke ctest to run our instrumented test binaries, causing .profraw files
            #   to be written in the coverage directory
            # - Invoke llvm-profdata to merge all .profraw files into coverage/merged.profdata
            # - Invoke llvm-cov to generate an HTML coverage report from the merged profile
            #   data and the test binaries, excluding system headers and test files
            set(COVERAGE_IGNORE_REGEX "'(/usr/|/tests/|/build/)'")
            add_custom_target(coverage
                DEPENDS coverage-clean dd_native_tests
                COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/coverage/test-%p-%m.profraw ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
                COMMAND ${LLVM_PROFDATA_BINARY} merge -sparse ${CMAKE_BINARY_DIR}/coverage/*.profraw -o ${CMAKE_BINARY_DIR}/coverage/merged.profdata
                COMMAND ${LLVM_COV_BINARY} show -format=html -output-dir=${CMAKE_BINARY_DIR}/coverage/html -ignore-filename-regex=${COVERAGE_IGNORE_REGEX} $<TARGET_FILE:dd_native_tests> -instr-profile=${CMAKE_BINARY_DIR}/coverage/merged.profdata
                COMMENT "Generating coverage report in ${CMAKE_BINARY_DIR}/coverage/html"
            )
        else()
            message(FATAL_ERROR "llvm-profdata and llvm-cov are required for coverage reporting. Install LLVM tools.")
        endif()
    endif()
endif()
