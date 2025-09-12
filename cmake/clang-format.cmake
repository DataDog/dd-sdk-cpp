# Store downloaded tools in tools/bin/
set(TOOLS_BIN_DIR "${DD_SDK_ROOT_DIR}/tools/bin")

# If clang-format support is not enabled for the build, skip the rest of this file
if(NOT DD_ENABLE_CLANG_FORMAT)
    return()
endif()

# For consistent formatting results, use this specific version of clang-format
# regardless of what compiler version (LLVM or otherwise) is used to build
set(CLANG_FORMAT_VERSION "20.1.8")
set(CLANG_FORMAT_BINARY_NAME "clang-format-20")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CLANG_FORMAT_BINARY "${TOOLS_BIN_DIR}/${CLANG_FORMAT_BINARY_NAME}.exe")
    set(CLANG_FORMAT_EXECUTABLE_NAME "clang-format.exe")
else()
    set(CLANG_FORMAT_BINARY "${TOOLS_BIN_DIR}/${CLANG_FORMAT_BINARY_NAME}")
    set(CLANG_FORMAT_EXECUTABLE_NAME "clang-format")
endif()

# Resolve the correct LLVM release archive, along with the path to bin/clang-format
# therein
set(CLANG_FORMAT_LLVM_ARCHIVE_NAME "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(CLANG_FORMAT_LLVM_ARCHIVE_NAME "LLVM-${CLANG_FORMAT_VERSION}-Linux-ARM64")
        set(CLANG_FORMAT_LLVM_ARCHIVE_SHA256 "b855cc17d935fdd83da82206b7a7cfc680095efd1e9e8182c4a05e761958bef8")
    else()
        set(CLANG_FORMAT_LLVM_ARCHIVE_NAME "LLVM-${CLANG_FORMAT_VERSION}-Linux-X64")
        set(CLANG_FORMAT_LLVM_ARCHIVE_SHA256 "1ead36b3dfcb774b57be530df42bec70ab2d239fbce9889447c7a29a4ddc1ae6")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(CLANG_FORMAT_LLVM_ARCHIVE_NAME "LLVM-${CLANG_FORMAT_VERSION}-macOS-ARM64")
    set(CLANG_FORMAT_LLVM_ARCHIVE_SHA256 "a9a22f450d35f1f73cd61ab6a17c6f27d8f6051d56197395c1eb397f0c9bbec4")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(CLANG_FORMAT_LLVM_ARCHIVE_NAME "clang+llvm-${CLANG_FORMAT_VERSION}-x86_64-pc-windows-msvc")
    set(CLANG_FORMAT_LLVM_ARCHIVE_SHA256 "f229769f11d6a6edc8ada599c0cda964b7dee6ab1a08c6cf9dd7f513e85b107f")
endif()

if(CLANG_FORMAT_LLVM_ARCHIVE_NAME)
    set(CLANG_FORMAT_URL "https://github.com/llvm/llvm-project/releases/download/llvmorg-${CLANG_FORMAT_VERSION}/${CLANG_FORMAT_LLVM_ARCHIVE_NAME}.tar.xz")
    set(CLANG_FORMAT_ARCHIVE_PATH "${CLANG_FORMAT_LLVM_ARCHIVE_NAME}/bin/${CLANG_FORMAT_EXECUTABLE_NAME}")
else()
    message(WARNING "Unsupported platform for automatic clang-format download: ${CMAKE_SYSTEM_NAME}")
endif()

# Downloading the full LLVM toolchain takes a while: if a compatible version of
# clang-format is already in the PATH (e.g. in CI), use it and skip the download
find_program(SYSTEM_CLANG_FORMAT NAMES "clang-format")
set(USE_SYSTEM_CLANG_FORMAT FALSE)
if(SYSTEM_CLANG_FORMAT)
    # Run clang-format --version to check the version
    execute_process(
        COMMAND "${SYSTEM_CLANG_FORMAT}" --version
        OUTPUT_VARIABLE CLANG_FORMAT_VERSION_OUTPUT
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE VERSION_CHECK_RESULT
    )
    if(VERSION_CHECK_RESULT EQUAL 0)
        # Parse the first line to check for "clang-format version x.y.z"
        string(REGEX MATCH "clang-format version ${CLANG_FORMAT_VERSION}" VERSION_MATCH "${CLANG_FORMAT_VERSION_OUTPUT}")
        if(VERSION_MATCH)
            message(STATUS "Found correct clang-format version in PATH: ${SYSTEM_CLANG_FORMAT}")
            set(USE_SYSTEM_CLANG_FORMAT TRUE)
            set(CLANG_FORMAT_BINARY "${SYSTEM_CLANG_FORMAT}")
        else()
            message(STATUS "System clang-format found but wrong version: ${CLANG_FORMAT_VERSION_OUTPUT}")
        endif()
    endif()
endif()

# If we don't have the desired version of clang-format in the PATH, and it's not yet
# been downloaded to tools/bin/, download the LLVM release archive and extract
# clang-format
if(NOT USE_SYSTEM_CLANG_FORMAT AND CLANG_FORMAT_URL AND NOT EXISTS "${CLANG_FORMAT_BINARY}")
    # Require an explicitly-set option before automatically downloading tools, as it
    # requires significant time, disk space, and security considerations
    if(NOT DD_DEVELOPMENT_ALLOW_AUTO_INSTALL)
        message(WARNING "Configuring with DD_ENABLE_CLANG_FORMAT, but clang-format ${CLANG_FORMAT_VERSION} is not in the PATH. Install it, or re-run with DD_DEVELOPMENT_ALLOW_AUTO_INSTALL.")
        return()
    endif()

    # Ensure that tools/bin/ exists
    file(MAKE_DIRECTORY "${TOOLS_BIN_DIR}")
    set(CLANG_FORMAT_ARCHIVE "${TOOLS_BIN_DIR}/${CLANG_FORMAT_LLVM_ARCHIVE_NAME}.tar.xz")
    message(STATUS "clang-format version ${CLANG_FORMAT_VERSION} not found in PATH; installing it.")

    # Fetch the LLVM release archive from GitHub
    message(STATUS "Downloading ${CLANG_FORMAT_LLVM_ARCHIVE_NAME} to ${TOOLS_BIN_DIR}...")
    file(DOWNLOAD
        "${CLANG_FORMAT_URL}"
        "${CLANG_FORMAT_ARCHIVE}"
        TLS_VERIFY ON
        EXPECTED_HASH "SHA256=${CLANG_FORMAT_LLVM_ARCHIVE_SHA256}"
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
    if(NOT DOWNLOAD_RESULT EQUAL 0)
        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_ERROR)
        message(FATAL_ERROR "Failed to download clang-format: ${DOWNLOAD_ERROR}")
    endif()
    
    # Extract the clang-format binary to tools/bin/${CLANG_FORMAT_ARCHIVE_PATH}
    message(STATUS "Extracting ${CLANG_FORMAT_ARCHIVE_PATH}...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf "${CLANG_FORMAT_ARCHIVE}" "${CLANG_FORMAT_ARCHIVE_PATH}"
        WORKING_DIRECTORY "${TOOLS_BIN_DIR}"
        RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to extract clang-format from archive")
    endif()
    
    # Copy clang-format directly to tools/bin/ and make it executable
    file(COPY "${TOOLS_BIN_DIR}/${CLANG_FORMAT_ARCHIVE_PATH}" DESTINATION "${TOOLS_BIN_DIR}")
    file(RENAME "${TOOLS_BIN_DIR}/${CLANG_FORMAT_EXECUTABLE_NAME}" "${CLANG_FORMAT_BINARY}")
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
        execute_process(COMMAND chmod +x "${CLANG_FORMAT_BINARY}")
    endif()
    
    # Remove the extracted directory path and delete the downloaded .tar.xz
    file(REMOVE "${CLANG_FORMAT_ARCHIVE}")
    file(REMOVE_RECURSE "${TOOLS_BIN_DIR}/${CLANG_FORMAT_LLVM_ARCHIVE_NAME}")
endif() 

# Verify that clang-format is available
if(USE_SYSTEM_CLANG_FORMAT)
    message(STATUS "Using system clang-format version ${CLANG_FORMAT_VERSION}: ${CLANG_FORMAT_BINARY}")
elseif(EXISTS "${CLANG_FORMAT_BINARY}")
    message(STATUS "Using downloaded clang-format version ${CLANG_FORMAT_VERSION}: ${CLANG_FORMAT_BINARY}")
else()
    message(WARNING "clang-format-${CLANG_FORMAT_VERSION} not available. Format checking will be disabled.")
    set(CLANG_FORMAT_BINARY "")
endif()

# Add custom targets to handle code formatting
if(CLANG_FORMAT_BINARY)
    # Collect all SDK source files that should be formatted
    set(FORMAT_DIRECTORIES examples include-c include-cpp src tests)
    set(FORMAT_EXTENSIONS h c hpp cpp)
    set(FORMAT_SOURCE_FILES "")
    foreach(DIR IN LISTS FORMAT_DIRECTORIES)
        foreach(EXT IN LISTS FORMAT_EXTENSIONS)
            file(GLOB_RECURSE DIR_FILES "${DD_SDK_ROOT_DIR}/${DIR}/*.${EXT}")
            list(APPEND FORMAT_SOURCE_FILES ${DIR_FILES})
        endforeach()
    endforeach()

    # 'format': run in-place formatting of all SDK source files
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_BINARY}" -i ${FORMAT_SOURCE_FILES}
        COMMENT "Formatting source code with clang-format"
        VERBATIM
    )

    # 'check-format': verify that all source files are properly formatted
    add_custom_target(check-format
        COMMAND "${CLANG_FORMAT_BINARY}" --dry-run --Werror ${FORMAT_SOURCE_FILES}
        COMMENT "Checking source code formatting with clang-format"
        VERBATIM
    )
endif()
