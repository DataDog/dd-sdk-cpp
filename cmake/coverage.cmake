# Coverage is supported when compiling with Clang, and we use the versions of
# llvm-profdata and llvm-cov distributed with our build toolchain
set(COVERAGE_SUPPORTED FALSE)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang$")
	set(COVERAGE_SUPPORTED TRUE)
endif()

# Enable code coverage flags on a per-target basis: note that we need to configure all
# targets with this function (even examples/benchmarks), to ensure that they link
# properly with the SDK library when compiled with coverage enabled
function(target_enable_coverage target_name)
    if(DD_ENABLE_COVERAGE)
        if(COVERAGE_SUPPORTED)
            target_compile_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping -O0)
            target_link_options(${target_name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        else()
            message(WARNING "Code coverage is only supported with Clang; binaries will not be instrumented for coverage (suppress with DD_ENABLE_COVERAGE=OFF)")
        endif()
    endif()
endfunction()

# Add custom targets for code coverage if enabled
if(DD_BUILD_TESTING AND DD_ENABLE_COVERAGE AND COVERAGE_SUPPORTED)
    # When coverage is enabled for a given target (via target_enable_coverage), the
    # Clang compiler injects instrumentation code into the binaries that writes
    # execution data to .profraw files at runtime. llvm-profdata merges these raw
    # profile files into an indexed .profdata file, and llvm-cov can generate coverage
    # reports from the .profdata file and the original binaries.
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
            DEPENDS coverage-clean tests
            COMMAND ${CMAKE_COMMAND} -E env LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/coverage/test-%p-%m.profraw ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR}
            COMMAND ${LLVM_PROFDATA_BINARY} merge -sparse ${CMAKE_BINARY_DIR}/coverage/*.profraw -o ${CMAKE_BINARY_DIR}/coverage/merged.profdata
            COMMAND ${LLVM_COV_BINARY} show -format=html -output-dir=${CMAKE_BINARY_DIR}/coverage/html -ignore-filename-regex=${COVERAGE_IGNORE_REGEX} $<TARGET_FILE:tests> -instr-profile=${CMAKE_BINARY_DIR}/coverage/merged.profdata
            COMMENT "Generating coverage report in ${CMAKE_BINARY_DIR}/coverage/html"
        )
    else()
        message(FATAL_ERROR "llvm-profdata and llvm-cov are required for coverage reporting. Install LLVM tools.")
    endif()
endif()
