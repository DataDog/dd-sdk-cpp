# Canonical version of clang-format and clang-tidy used to format and lint SDK source
set(LLVM_TOOLS_VERSION "20.1.8")

# Use llvm-bin/ to store auto-downloaded clang-format and/or clang-tidy if they're not
# already installed on the system
set(LOCAL_LLVM_BIN_DIR "${DD_SDK_ROOT_DIR}/llvm-bin")

# Compute paths to locally-installed tools, if they're present
set(LOCAL_CLANG_FORMAT_BINARY_NAME "clang-format-${LLVM_TOOLS_VERSION}")
set(LOCAL_CLANG_TIDY_BINARY_NAME "clang-tidy-${LLVM_TOOLS_VERSION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(LOCAL_CLANG_FORMAT_BINARY_NAME "${LOCAL_CLANG_FORMAT_BINARY_NAME}.exe")
    set(LOCAL_CLANG_TIDY_BINARY_NAME "${LOCAL_CLANG_TIDY_BINARY_NAME}.exe")
endif()
set(LOCAL_CLANG_FORMAT_BINARY "${LOCAL_LLVM_BIN_DIR}/${LOCAL_CLANG_FORMAT_BINARY_NAME}")
set(LOCAL_CLANG_TIDY_BINARY "${LOCAL_LLVM_BIN_DIR}/${LOCAL_CLANG_TIDY_BINARY_NAME}")

# If we want clang-format, attempt to find an already-installed version at our desired
# version, and set CLANG_FORMAT_BINARY if found
if(DD_ENABLE_CLANG_FORMAT AND NOT CLANG_FORMAT_BINARY)
    # If we have a local binary downloaded already, we can use it
    if (EXISTS "${LOCAL_CLANG_FORMAT_BINARY}")
        set(CLANG_FORMAT_BINARY "${LOCAL_CLANG_FORMAT_BINARY}")
        message(STATUS "Using local clang-format ${LLVM_TOOLS_VERSION}: ${CLANG_FORMAT_BINARY}")
    else()
        # Otherwise, check the system PATH for a version that meets our requirement
        find_program(SYSTEM_CLANG_FORMAT NAMES "clang-format")
        if(SYSTEM_CLANG_FORMAT)
            # Run clang-format --version to check the version
            execute_process(
                COMMAND "${SYSTEM_CLANG_FORMAT}" --version
                OUTPUT_VARIABLE CLANG_FORMAT_VERSION_OUTPUT
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE CLANG_FORMAT_VERSION_RESULT
            )
            if(CLANG_FORMAT_VERSION_RESULT EQUAL 0)
                # Parse the output check for "version x.y.z"
                string(REGEX MATCH "version ${LLVM_TOOLS_VERSION}" VERSION_MATCH "${CLANG_FORMAT_VERSION_OUTPUT}")
                if(VERSION_MATCH)
                    message(STATUS "clang-format found in PATH at required version: ${SYSTEM_CLANG_FORMAT}")
                    set(CLANG_FORMAT_BINARY "${SYSTEM_CLANG_FORMAT}")
                else()
                    message(STATUS "clang-format found in PATH, but version ${LLVM_TOOLS_VERSION} is required. Got: ${LLVM_TOOLS_VERSION} is required. Got: ${CLANG_FORMAT_VERSION_OUTPUT}")
                endif()
            endif()
        endif()
    endif()
endif()

# If we want clang-tidy, attempt to find an already-installed version at our desired
# version, and set CLANG_TIDY_BINARY if found
if(DD_ENABLE_CLANG_TIDY AND NOT CLANG_TIDY_BINARY)
    # If we have a local binary downloaded already, we can use it
    if (EXISTS "${LOCAL_CLANG_TIDY_BINARY}")
        set(CLANG_TIDY_BINARY "${LOCAL_CLANG_TIDY_BINARY}")
        message(STATUS "Using local clang-tidy ${LLVM_TOOLS_VERSION}: ${CLANG_TIDY_BINARY}")
    else()
        # Otherwise, check the system PATH for a version that meets our requirement
        find_program(SYSTEM_CLANG_TIDY NAMES "clang-tidy")
        if(SYSTEM_CLANG_TIDY)
            # Run clang-tidy --version to check the version
            execute_process(
                COMMAND "${SYSTEM_CLANG_TIDY}" --version
                OUTPUT_VARIABLE CLANG_TIDY_VERSION_OUTPUT
                ERROR_QUIET
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE CLANG_TIDY_VERSION_RESULT
            )
            if(CLANG_TIDY_VERSION_RESULT EQUAL 0)
                # Parse the output check for "version x.y.z"
                string(REGEX MATCH "version ${LLVM_TOOLS_VERSION}" VERSION_MATCH "${CLANG_TIDY_VERSION_OUTPUT}")
                if(VERSION_MATCH)
                    message(STATUS "clang-tidy found in PATH at required version: ${SYSTEM_CLANG_TIDY}")
                    set(CLANG_TIDY_BINARY "${SYSTEM_CLANG_TIDY}")
                else()
                    message(STATUS "clang-tidy found in PATH, but version ${LLVM_TOOLS_VERSION} is required. Got: ${LLVM_TOOLS_VERSION} is required. Got: ${CLANG_TIDY_VERSION_OUTPUT}")
                endif()
            endif()
        endif()
    endif()
endif()

# Determine whether we're missing required tools and need to download an LLVM release in
# order to install them locally
set(NEEDS_LLVM_DOWNLOAD FALSE)
if(DD_ENABLE_CLANG_FORMAT AND NOT CLANG_FORMAT_BINARY)
    set(NEEDS_LLVM_DOWNLOAD TRUE)
endif()
if(DD_ENABLE_CLANG_TIDY AND NOT CLANG_TIDY_BINARY)
    set(NEEDS_LLVM_DOWNLOAD TRUE)
endif()

# Sort out options to make sure we're able to proceed with the download
set(SHOULD_DOWNLOAD_LLVM FALSE)
if(NEEDS_LLVM_DOWNLOAD)
    message(STATUS "Build is configured with:\n -DDD_ENABLE_CLANG_FORMAT=${DD_ENABLE_CLANG_FORMAT} (requires clang-format ${LLVM_TOOLS_VERSION})\n -DDD_ENABLE_CLANG_TIDY=${DD_ENABLE_CLANG_TIDY} (requires clang-tidy ${LLVM_TOOLS_VERSION})\n Not all tools are installed at the required versions.")
    if(NOT DD_DEVELOPMENT_ALLOW_AUTO_INSTALL)
        # Without explicit permission to auto-install, we have no way to proceed.
        # Fail fast with an actionable message rather than letting the build break
        # further down with a less helpful error.
        message(FATAL_ERROR
            "Required development tools are not installed at version ${LLVM_TOOLS_VERSION}.\n"
            "\n"
            "Choose one of the following:\n"
            "  1. Let CMake download them automatically (recommended):\n"
            "       -DDD_DEVELOPMENT_ALLOW_AUTO_INSTALL=ON\n"
            "  2. Install LLVM ${LLVM_TOOLS_VERSION} manually and ensure its bin/\n"
            "     directory is in your PATH.\n"
            "  3. Disable these tools for this build:\n"
            "       -DDD_ENABLE_CLANG_FORMAT=OFF -DDD_ENABLE_CLANG_TIDY=OFF\n"
            "\n"
            "See CONTRIBUTING.md (section: Formatting and linting) for details."
        )
    else()
        # Auto-install is enabled; we can proceed to download and install tools locally
        message(STATUS "Required tools will be automatically downloaded and installed to ${LOCAL_LLVM_BIN_DIR}.")
        set(SHOULD_DOWNLOAD_LLVM TRUE)
    endif()
endif()

# Proceed with the download, extracting all required tools
if(SHOULD_DOWNLOAD_LLVM)
    # Resolve the correct LLVM release archive from the llvm-project GitHub repo
    set(LLVM_ARCHIVE_NAME "")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
            set(LLVM_ARCHIVE_NAME "LLVM-${LLVM_TOOLS_VERSION}-Linux-ARM64")
            set(LLVM_ARCHIVE_SHA256 "b855cc17d935fdd83da82206b7a7cfc680095efd1e9e8182c4a05e761958bef8")
        else()
            set(LLVM_ARCHIVE_NAME "LLVM-${LLVM_TOOLS_VERSION}-Linux-X64")
            set(LLVM_ARCHIVE_SHA256 "1ead36b3dfcb774b57be530df42bec70ab2d239fbce9889447c7a29a4ddc1ae6")
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(LLVM_ARCHIVE_NAME "LLVM-${LLVM_TOOLS_VERSION}-macOS-ARM64")
        set(LLVM_ARCHIVE_SHA256 "a9a22f450d35f1f73cd61ab6a17c6f27d8f6051d56197395c1eb397f0c9bbec4")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(LLVM_ARCHIVE_NAME "clang+llvm-${LLVM_TOOLS_VERSION}-x86_64-pc-windows-msvc")
        set(LLVM_ARCHIVE_SHA256 "f229769f11d6a6edc8ada599c0cda964b7dee6ab1a08c6cf9dd7f513e85b107f")
    endif()

    # Build the URL for the desired archive
    if(LLVM_ARCHIVE_NAME)
        set(LLVM_ARCHIVE_URL "https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_TOOLS_VERSION}/${LLVM_ARCHIVE_NAME}.tar.xz")
        message(STATUS "URL: ${LLVM_ARCHIVE_URL}")
    else()
        message(FATAL_ERROR "Unsupported platform for automatic LLVM tool download: ${CMAKE_SYSTEM_NAME}")
    endif()

    # Ensure that llvm-bin/ exists
    file(MAKE_DIRECTORY "${LOCAL_LLVM_BIN_DIR}")
    set(LOCAL_LLVM_ARCHIVE "${LOCAL_LLVM_BIN_DIR}/${LLVM_ARCHIVE_NAME}.tar.xz")

    # Fetch the LLVM release archive from GitHub. A stale, partial, or corrupt
    # file on disk will fail the EXPECTED_HASH check and, by default, CMake
    # leaves the bad file in place — so subsequent configures fail the same
    # way forever. Retry once after deleting the cached file before giving up.
    set(_DD_LLVM_DOWNLOAD_ATTEMPTS 2)
    foreach(_DD_LLVM_ATTEMPT RANGE 1 ${_DD_LLVM_DOWNLOAD_ATTEMPTS})
        message(STATUS "Downloading LLVM ${LLVM_TOOLS_VERSION} to ${LOCAL_LLVM_ARCHIVE} (attempt ${_DD_LLVM_ATTEMPT}/${_DD_LLVM_DOWNLOAD_ATTEMPTS})...")
        file(DOWNLOAD
            "${LLVM_ARCHIVE_URL}"
            "${LOCAL_LLVM_ARCHIVE}"
            TLS_VERIFY ON
            EXPECTED_HASH "SHA256=${LLVM_ARCHIVE_SHA256}"
            STATUS DOWNLOAD_STATUS
            SHOW_PROGRESS
        )
        list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
        if(DOWNLOAD_RESULT EQUAL 0)
            break()
        endif()

        list(GET DOWNLOAD_STATUS 1 DOWNLOAD_ERROR)
        if(_DD_LLVM_ATTEMPT LESS ${_DD_LLVM_DOWNLOAD_ATTEMPTS})
            message(STATUS "Download failed (${DOWNLOAD_ERROR}); removing cached archive and retrying...")
            file(REMOVE "${LOCAL_LLVM_ARCHIVE}")
        else()
            file(REMOVE "${LOCAL_LLVM_ARCHIVE}")
            message(FATAL_ERROR
                "Failed to download LLVM archive after ${_DD_LLVM_DOWNLOAD_ATTEMPTS} attempts: ${DOWNLOAD_ERROR}\n"
                "If this persists, download it manually and place it at ${LOCAL_LLVM_ARCHIVE} "
                "(expected SHA256: ${LLVM_ARCHIVE_SHA256}), then re-run cmake."
            )
        endif()
    endforeach()
    unset(_DD_LLVM_ATTEMPT)
    unset(_DD_LLVM_DOWNLOAD_ATTEMPTS)

    # Resolve the paths (within the archive) to the binaries we need to extract
    if(DD_ENABLE_CLANG_FORMAT AND NOT CLANG_FORMAT_BINARY)
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(CLANG_FORMAT_ARCHIVE_PATH "${LLVM_ARCHIVE_NAME}/bin/clang-format.exe")
            set(CLANG_FORMAT_ARCHIVE_FILENAME "clang-format.exe")
        else()
            set(CLANG_FORMAT_ARCHIVE_PATH "${LLVM_ARCHIVE_NAME}/bin/clang-format")
            set(CLANG_FORMAT_ARCHIVE_FILENAME "clang-format")
        endif()
    endif()
    if(DD_ENABLE_CLANG_TIDY AND NOT CLANG_TIDY_BINARY)
        if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
            set(CLANG_TIDY_ARCHIVE_PATH "${LLVM_ARCHIVE_NAME}/bin/clang-tidy.exe")
            set(CLANG_TIDY_ARCHIVE_FILENAME "clang-tidy.exe")
        else()
            set(CLANG_TIDY_ARCHIVE_PATH "${LLVM_ARCHIVE_NAME}/bin/clang-tidy")
            set(CLANG_TIDY_ARCHIVE_FILENAME "clang-tidy")
        endif()
    endif()

    # If we don't already have a clang-format binary, extract the one from our archive
    if(CLANG_FORMAT_ARCHIVE_PATH)
        # Extract to llvm-bin/${CLANG_FORMAT_ARCHIVE_PATH}
        message(STATUS "Extracting ${CLANG_FORMAT_ARCHIVE_PATH}...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${LOCAL_LLVM_ARCHIVE}" "${CLANG_FORMAT_ARCHIVE_PATH}"
            WORKING_DIRECTORY "${LOCAL_LLVM_BIN_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract clang-format from archive")
        endif()

        # Copy to llvm-bin/clang-format-{version} and chmod +x
        file(COPY "${LOCAL_LLVM_BIN_DIR}/${CLANG_FORMAT_ARCHIVE_PATH}" DESTINATION "${LOCAL_LLVM_BIN_DIR}")
        file(RENAME "${LOCAL_LLVM_BIN_DIR}/${CLANG_FORMAT_ARCHIVE_FILENAME}" "${LOCAL_CLANG_FORMAT_BINARY}")
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
            execute_process(COMMAND chmod +x "${LOCAL_CLANG_FORMAT_BINARY}")
        endif()

        # Set CLANG_FORMAT_BINARY
        set(CLANG_FORMAT_BINARY "${LOCAL_CLANG_FORMAT_BINARY}")
        message(STATUS "Using local clang-format ${LLVM_TOOLS_VERSION}: ${CLANG_FORMAT_BINARY}")
    endif()

    # If we don't already have a clang-tidy binary, extract the one from our archive
    if(CLANG_TIDY_ARCHIVE_PATH)
        # Extract to llvm-bin/${CLANG_TIDY_ARCHIVE_PATH}
        message(STATUS "Extracting ${CLANG_TIDY_ARCHIVE_PATH}...")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xf "${LOCAL_LLVM_ARCHIVE}" "${CLANG_TIDY_ARCHIVE_PATH}"
            WORKING_DIRECTORY "${LOCAL_LLVM_BIN_DIR}"
            RESULT_VARIABLE EXTRACT_RESULT
        )
        if(NOT EXTRACT_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to extract clang-tidy from archive")
        endif()

        # Copy to llvm-bin/clang-tidy-{version} and chmod +x
        file(COPY "${LOCAL_LLVM_BIN_DIR}/${CLANG_TIDY_ARCHIVE_PATH}" DESTINATION "${LOCAL_LLVM_BIN_DIR}")
        file(RENAME "${LOCAL_LLVM_BIN_DIR}/${CLANG_TIDY_ARCHIVE_FILENAME}" "${LOCAL_CLANG_TIDY_BINARY}")
        if(NOT CMAKE_SYSTEM_NAME STREQUAL "Windows")
            execute_process(COMMAND chmod +x "${LOCAL_CLANG_TIDY_BINARY}")
        endif()

        # Set CLANG_TIDY_BINARY
        set(CLANG_TIDY_BINARY "${LOCAL_CLANG_TIDY_BINARY}")
        message(STATUS "Using local clang-tidy ${LLVM_TOOLS_VERSION}: ${CLANG_TIDY_BINARY}")
    endif()

    # Remove the extracted directory path and delete the downloaded .tar.xz
    file(REMOVE_RECURSE "${LOCAL_LLVM_BIN_DIR}/${LLVM_ARCHIVE_NAME}")
    file(REMOVE "${LOCAL_LLVM_ARCHIVE}")
    message(STATUS "${LOCAL_LLVM_ARCHIVE} deleted.")
endif()
