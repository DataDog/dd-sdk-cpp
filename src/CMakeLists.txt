# We support building the library as either static or shared
if(DD_BUILD_SHARED)
    set(DD_LIBRARY_TYPE SHARED)
else()
    set(DD_LIBRARY_TYPE STATIC)
endif()

# Main library target, with both C and C++ APIs
add_library(dd_native ${DD_LIBRARY_TYPE}
    datadog/c/attribute.cpp
    datadog/c/core.cpp
    datadog/c/crash_reporting.cpp
    datadog/c/logging.cpp
    datadog/c/rum.cpp
    datadog/c/timestamp.cpp
    datadog/c/uuid.cpp
    datadog/c/version.cpp
    datadog/cpp/attribute.cpp
    datadog/cpp/core.cpp
    datadog/cpp/crash_reporting.cpp
    datadog/cpp/logging.cpp
    datadog/cpp/rum.cpp
    datadog/cpp/uuid.cpp
    datadog/cpp/version.cpp
    datadog/impl/attribute/cow.cpp
    datadog/impl/attribute/merge.cpp
    datadog/impl/core/context.cpp
    datadog/impl/core/core.cpp
    datadog/impl/core/feature_read.cpp
    datadog/impl/core/feature_scope.cpp
    datadog/impl/core/feature.cpp
    datadog/impl/core/storage_write.cpp
    datadog/impl/core/storage_queue.cpp
    datadog/impl/core/storage_thread.cpp
    datadog/impl/core/tlv.cpp
    datadog/impl/core/upload_scheduler.cpp
    datadog/impl/core/upload_thread.cpp
    datadog/impl/core/writer.cpp
    datadog/impl/features/logging/logger.cpp
    datadog/impl/features/logging/logging.cpp
    datadog/impl/features/crash_reporting/crash_reporting.cpp
    datadog/impl/features/rum/containers/resource_map.cpp
    datadog/impl/features/rum/containers/view_array.cpp
    datadog/impl/features/rum/scopes/action.cpp
    datadog/impl/features/rum/scopes/application.cpp
    datadog/impl/features/rum/scopes/resource.cpp
    datadog/impl/features/rum/scopes/session.cpp
    datadog/impl/features/rum/scopes/view.cpp
    datadog/impl/features/rum/context.cpp
    datadog/impl/features/rum/rum.cpp
    datadog/impl/features/rum/scope.cpp
    datadog/impl/json/primitives/bool.cpp
    datadog/impl/json/primitives/float.cpp
    datadog/impl/json/primitives/integer.cpp
    datadog/impl/json/primitives/null.cpp
    datadog/impl/json/primitives/string.cpp
    datadog/impl/json/primitives/timestamp.cpp
    datadog/impl/json/primitives/uuid.cpp
    datadog/impl/json/attribute.cpp
    datadog/impl/json/diagnostic_attribute.cpp
    datadog/impl/storage/path.cpp
    datadog/impl/diagnostics.cpp
    datadog/impl/error_message.cpp
)

# Inject version number from CMake project config
target_compile_definitions(dd_native PUBLIC DATADOG_BUILD_VERSION="${ddsdkcpp_VERSION}")
if(DD_GIT_REVISION_ID)
    target_compile_definitions(dd_native PUBLIC DATADOG_GIT_REVISION_ID="${DD_GIT_REVISION_ID}")
endif()

# Enable position-independent code for ASLR, shared library usage, etc.
set_target_properties(dd_native PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Any C++ project that depends on this target must compile with C++17 or newer
target_compile_features(dd_native PUBLIC cxx_std_17)

# Add internal-only, header-only dependencies as PRIVATE SYSTEM includes
target_include_external_deps(dd_native)

# Use strict compiler warnings, warnings-as-errors, when building the SDK
target_enable_strict_warnings(dd_native)

# Conditionally define WITH_DATADOG_ASSERTS based on build config
target_enable_datadog_assert(dd_native)

# Conditionally define DATADOG_SHARED_LIB, WITH_DATADOG_EXPORTS
target_enable_shared_library_support(dd_native)

# Add clang-tidy to the build if enabled
target_enable_clang_tidy(dd_native)

# Compile and link with profile instrumentation if coverage is enabled
target_enable_coverage(dd_native)

# Compile and link with sanitizers if enabled
target_enable_sanitizers(dd_native)

# Configure include paths: when building the library, pull in public includes from
# include-c/ and include-cpp/; when using an installed build, resolve public headers
# from the build's include dir
target_include_directories(dd_native
    PUBLIC 
        $<BUILD_INTERFACE:${DD_SDK_ROOT_DIR}/include-c>
        $<BUILD_INTERFACE:${DD_SDK_ROOT_DIR}/include-cpp>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        .
)

# On Win32, we implement UUID generation via CoCreateGuid from objbase.h; on other
# systems we require uuid/uuid.h
if(NOT WIN32)
    # On macOS, uuid/uuid.h is a system header and `uuid_generate()` et al. are
    # implemented in libc. But on Linux, uuid/uuid.h is typically not available by
    # default and must be installed from a package
    include(CheckIncludeFiles)
    check_include_files("uuid/uuid.h" HAVE_UUID_H)
    if(NOT HAVE_UUID_H)
        message(FATAL_ERROR "uuid/uuid.h not found in include path. Please install the UUID development package:\n"
                           "  Ubuntu/Debian: sudo apt-get install uuid-dev\n"
                           "  RHEL/CentOS/Fedora: sudo yum install libuuid-devel\n"
                           "  Alpine: sudo apk add util-linux-dev")
    endif()

    # On Linux, we need to link against this separate uuid library
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_library(UUID_LIBRARY uuid)
        if(NOT UUID_LIBRARY)
            message(FATAL_ERROR "UUID library not found")
        endif()
        target_link_libraries(dd_native PRIVATE ${UUID_LIBRARY})
    endif()
endif()

# Platform-specific support for system clock functionality
option(DD_CLOCK_USE_DEFAULT "Use default clock implementation. If disabled, you must supply an implementation of platform/clock.hpp in a file specified as DD_CLOCK_SRC" ON)
if(DD_CLOCK_USE_DEFAULT)
    target_sources(dd_native PRIVATE datadog/impl/platform/clock_std.cpp)
else()
    set(DD_CLOCK_SRC "" CACHE STRING "Path to custom implementation of platform/clock.hpp")
    if(DD_CLOCK_SRC)
        target_sources(dd_native PRIVATE ${DD_CLOCK_SRC})
    endif()
endif()

# Platform-specific support for file I/O
option(DD_FILESYSTEM_USE_DEFAULT "Use default filesystem implementation. If disabled, you must supply an implementation of platform/filesystem.hpp in a file specified as DD_FILESYSTEM_SRC" ON)
if(DD_FILESYSTEM_USE_DEFAULT)
    target_sources(dd_native PRIVATE datadog/impl/platform/filesystem_std.cpp)
else()
    set(DD_FILESYSTEM_SRC "" CACHE STRING "Path to custom implementation of platform/filesystem.hpp")
    if(DD_FILESYSTEM_SRC)
        target_sources(dd_native PRIVATE ${DD_FILESYSTEM_SRC})
    endif()
endif()

# Platform-specific support for HTTP client functionality
option(DD_HTTP_USE_DEFAULT "Use default HTTP client implementation. If disabled, you must supply an implementation of platform/http.hpp in a file specified as DD_HTTP_SRC" ON)
if(DD_HTTP_USE_DEFAULT)
    option(DD_HTTP_USE_SYSTEM_LIBCURL "Link against the system-installed libcurl as a shared-library dependency" ON)
    if(DD_HTTP_USE_SYSTEM_LIBCURL)
        find_package(CURL REQUIRED)
    else()
        include(${DD_SDK_ROOT_DIR}/cmake/deps/libcurl.cmake)
    endif()
    target_link_libraries(dd_native PRIVATE CURL::libcurl)
    get_target_property(CURL_INCLUDE_DIRS CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(dd_native SYSTEM PRIVATE ${CURL_INCLUDE_DIRS})
    target_sources(dd_native PRIVATE datadog/impl/platform/http_curl.cpp)
else()
    set(DD_HTTP_SRC "" CACHE STRING "Path to custom implementation of platform/http.hpp")
    if(DD_HTTP_SRC)
        target_sources(dd_native PRIVATE ${DD_HTTP_SRC})
    endif()
endif()

# Platform-specific support for system info
option(DD_SYSTEMINFO_USE_DEFAULT "Use default system info implementation. If disabled, you must supply an implementation of platform/system_info.hpp in a file specified as DD_SYSTEMINFO_SRC" ON)
if(DD_SYSTEMINFO_USE_DEFAULT)
    if(WIN32)
        target_sources(dd_native PRIVATE datadog/impl/platform/system_info_windows.cpp)
    elseif(APPLE)
        target_sources(dd_native PRIVATE datadog/impl/platform/system_info_macos.cpp)
        target_link_libraries(dd_native PRIVATE "-framework CoreFoundation")
    elseif(UNIX)
        target_sources(dd_native PRIVATE datadog/impl/platform/system_info_linux.cpp)
    endif()
else()
    set(DD_SYSTEMINFO_SRC "" CACHE STRING "Path to custom implementation of platform/system_info.hpp")
    if(DD_SYSTEMINFO_SRC)
        target_sources(dd_native PRIVATE ${DD_SYSTEMINFO_SRC})
    endif()
endif()

# Crash handler implementation selection, based on DD_CRASH_MODE
if(DD_CRASH_MODE STREQUAL "noop")
    target_sources(dd_native PRIVATE datadog/impl/platform/crash_handler_noop.cpp)
elseif(DD_CRASH_MODE STREQUAL "inprocess")
    # Select platform-specific in-process crash handler implementation
    if(WIN32)
        target_sources(dd_native PRIVATE datadog/impl/platform/crash_handler_inprocess_windows.cpp)
    elseif(UNIX)
        # Unified POSIX implementation for Linux and macOS
        target_sources(dd_native PRIVATE datadog/impl/platform/crash_handler_inprocess_posix.cpp)
        target_compile_options(dd_native PRIVATE -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "In-process crash handler not supported on this platform")
    endif()

    # In-process implementation needs to write crash report files
    target_sources(dd_native PRIVATE datadog/impl/platform/crash_report_write.cpp)
elseif(DD_CRASH_MODE STREQUAL "crashpad")
    # If building with crashpad support, integrate crashpad as a dependency: this
    # adds an ExternalProject target that will build crashpad, then adds the
    # resulting static lib as a dependency for dd_native. To ensure
    # binary-compatible builds, this script must run after compile/link options for
    # dd_native are fully configured.
    include(${DD_SDK_ROOT_DIR}/cmake/crashpad.cmake)
    target_sources(dd_native PRIVATE datadog/impl/platform/crash_handler_crashpad.cpp)
else()
    message(FATAL_ERROR "Invalid DD_CRASH_MODE: ${DD_CRASH_MODE}. Must be 'noop', 'inprocess', or 'crashpad'")
endif()

# Install configuration
if(DD_BUILD_INSTALL)
    install(TARGETS dd_native
        EXPORT DatadogTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()
