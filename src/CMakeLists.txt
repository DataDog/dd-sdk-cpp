# We support building the library as either static or shared
if(DD_BUILD_SHARED)
    set(DD_LIBRARY_TYPE SHARED)
else()
    set(DD_LIBRARY_TYPE STATIC)
endif()

# Main library target, with both C and C++ APIs
add_library(ddsdkcpp ${DD_LIBRARY_TYPE}
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
    datadog/impl/core/
    datadog/impl/core/attribute/binary.cpp
    datadog/impl/core/attribute/cow.cpp
    datadog/impl/core/attribute/merge.cpp
    datadog/impl/core/json/primitives/bool.cpp
    datadog/impl/core/json/primitives/float.cpp
    datadog/impl/core/json/primitives/integer.cpp
    datadog/impl/core/json/primitives/null.cpp
    datadog/impl/core/json/primitives/string.cpp
    datadog/impl/core/json/primitives/timestamp.cpp
    datadog/impl/core/json/primitives/uuid.cpp
    datadog/impl/core/json/attribute.cpp
    datadog/impl/core/json/diagnostic_attribute.cpp
    datadog/impl/core/util/diagnostics.cpp
    datadog/impl/core/util/validation.cpp
    datadog/impl/core/context_thread.cpp
    datadog/impl/core/context.cpp
    datadog/impl/core/core.cpp
    datadog/impl/core/feature_read.cpp
    datadog/impl/core/feature_scope.cpp
    datadog/impl/core/feature.cpp
    datadog/impl/core/message_bus.cpp
    datadog/impl/core/messaging_thread.cpp
    datadog/impl/core/storage_queue.cpp
    datadog/impl/core/storage_thread.cpp
    datadog/impl/core/storage_write.cpp
    datadog/impl/core/tlv.cpp
    datadog/impl/core/upload_scheduler.cpp
    datadog/impl/core/upload_thread.cpp
    datadog/impl/core/upload_util.cpp
)

# core/storage, with IFilesystem implementation determined per-platform
target_sources(ddsdkcpp PRIVATE
    datadog/impl/core/storage/artifact.cpp
    datadog/impl/core/storage/feature_event.cpp
    datadog/impl/core/storage/filesystem_wrapper.cpp
    datadog/impl/core/storage/path.cpp
    datadog/impl/core/storage/sdk.cpp
    datadog/impl/core/storage/util.cpp
)
if(WIN32)
    target_sources(ddsdkcpp PRIVATE datadog/impl/core/storage/filesystem_windows.cpp)
else()
    target_sources(ddsdkcpp PRIVATE datadog/impl/core/storage/filesystem_posix.cpp)
endif()

# core/http, with IHttpClient implementation using libcurl on POSIX, WinHTTP on Win32
target_sources(ddsdkcpp PRIVATE
    datadog/impl/core/http/body_writer_string.cpp
    datadog/impl/core/http/body_writer_tlv.cpp
    datadog/impl/core/http/request_builder.cpp
)

# We support different HTTP client implementations:
# - DD_HTTP_CLIENT=libcurl (default)
# - DD_HTTP_CLIENT=none    (define no client; for use by cross-platform SDKs)
set(DD_HTTP_CLIENT "libcurl" CACHE STRING "HTTP client backend (libcurl, none)")
set_property(CACHE DD_HTTP_CLIENT PROPERTY STRINGS "libcurl" "none")

if(DD_HTTP_CLIENT STREQUAL "libcurl")
    # Use the internal IHttpClient implementation that depends on libcurl
    target_sources(ddsdkcpp PRIVATE datadog/impl/core/http/client_curl.cpp)

    # Default to vendoring libcurl if using it on Windows, since libcurl DLLs don't ship
    # with Windows; default to using the system-installed libcurl on POSIX
    if(WIN32)
        set(DD_HTTP_USE_SYSTEM_LIBCURL_DEFAULT OFF)
    else()
        set(DD_HTTP_USE_SYSTEM_LIBCURL_DEFAULT ON)
    endif()
    option(DD_HTTP_USE_SYSTEM_LIBCURL "Link against the system-installed libcurl as a shared-library dependency" "${DD_HTTP_USE_SYSTEM_LIBCURL_DEFAULT}")
    if(DD_HTTP_USE_SYSTEM_LIBCURL)
        find_package(CURL REQUIRED)
    else()
        include(${DD_SDK_ROOT_DIR}/cmake/deps/libcurl.cmake)
    endif()

    # Add libcurl as a linker dependency and make its headers available
    target_link_libraries(ddsdkcpp PRIVATE CURL::libcurl)
    get_target_property(CURL_INCLUDE_DIRS CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(ddsdkcpp SYSTEM PRIVATE ${CURL_INCLUDE_DIRS})
elseif(DD_HTTP_CLIENT STREQUAL "none")
    # The SDK will be built without an HTTP client implementation. This option is
    # intended for Datadog cross-platform SDKs that want to bring their own HTTP client
    # implementation: they would build the SDK as a static library (with the relevant
    # symbols referenced but not defined), then link a custom IHttpClient implementation
    # into the final build alongside that static library.
    if(DD_BUILD_SHARED)
        message(FATAL_ERROR "DD_HTTP_CLIENT=none is incompatible with DD_BUILD_SHARED=ON: a shared library cannot have unresolved symbols")
    endif()
    message(WARNING "Configured with DD_HTTP_CLIENT=none; resulting static library will be unusable without defining an IHttpClient implementation in a downstream build")
else()
    message(FATAL_ERROR "Invalid DD_HTTP_CLIENT: ${DD_HTTP_CLIENT}. Must be 'libcurl' or 'none'")
endif()

# Inject version number from CMake project config
target_compile_definitions(ddsdkcpp PUBLIC DATADOG_BUILD_VERSION="${ddsdkcpp_VERSION}")
if(DD_GIT_REVISION_ID)
    target_compile_definitions(ddsdkcpp PUBLIC DATADOG_GIT_REVISION_ID="${DD_GIT_REVISION_ID}")
endif()

# When building a universal (multi-architecture) binary, the preprocessor arch
# detection in version.hpp would resolve to whichever slice is being compiled.
# Signal the universal case explicitly so the artifact name reflects it.
list(LENGTH CMAKE_OSX_ARCHITECTURES _dd_arch_count)
if(_dd_arch_count GREATER 1)
    target_compile_definitions(ddsdkcpp PUBLIC DATADOG_BUILD_IS_UNIVERSAL)
endif()

# Enable position-independent code for ASLR, shared library usage, etc.
set_target_properties(ddsdkcpp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME ddsdkcpp
    EXPORT_NAME sdk
)

# Building the SDK requires either C++17 or C++20 at a minimum, depending on
# configuration. (The C++ API adheres to C++17, and the C API adheres to C99.)
if(DD_CRASH_MODE STREQUAL "crashpad")
    target_compile_features(ddsdkcpp PUBLIC cxx_std_20)
else()
    target_compile_features(ddsdkcpp PUBLIC cxx_std_17)
endif()

# Add internal-only, header-only dependencies as PRIVATE SYSTEM includes
target_include_external_deps(ddsdkcpp)

# Use strict compiler warnings, warnings-as-errors, when building the SDK
target_enable_strict_warnings(ddsdkcpp)
target_enable_parallel_compilation(ddsdkcpp)

# Conditionally define WITH_DATADOG_ASSERTS based on build config
target_enable_datadog_assert(ddsdkcpp)

# Conditionally define DATADOG_SHARED_LIB, WITH_DATADOG_EXPORTS
target_enable_shared_library_support(ddsdkcpp)

# Add clang-tidy to the build if enabled
target_enable_clang_tidy(ddsdkcpp)

# Compile and link with profile instrumentation if coverage is enabled
target_enable_coverage(ddsdkcpp)

# Compile and link with sanitizers if enabled
target_enable_sanitizers(ddsdkcpp)

# Configure include paths: when building the library, pull in public includes from
# include-c/ and include-cpp/; when using an installed build, resolve public headers
# from the build's include dir
target_include_directories(ddsdkcpp
    PUBLIC
        $<BUILD_INTERFACE:${DD_SDK_ROOT_DIR}/include-c>
        $<BUILD_INTERFACE:${DD_SDK_ROOT_DIR}/include-cpp>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        .
)

# Platform-specific support for UUID generation
option(DD_UUID_USE_DEFAULT "Use default UUID implementation. If disabled, you must supply an implementation of platform/uuid.hpp in a file specified as DD_UUID_SRC" ON)
if(DD_UUID_USE_DEFAULT)
    target_sources(ddsdkcpp PRIVATE datadog/impl/core/platform/uuid_std.cpp)
    # On Win32, the default implementation uses CoCreateGuid from objbase.h; on
    # other systems it requires uuid/uuid.h
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
            target_link_libraries(ddsdkcpp PRIVATE ${UUID_LIBRARY})
        endif()
    endif()
else()
    set(DD_UUID_SRC "" CACHE STRING "Path to custom implementation of platform/uuid.hpp")
    if(DD_UUID_SRC)
        target_sources(ddsdkcpp PRIVATE ${DD_UUID_SRC})
    endif()
endif()

# Platform-specific support for system clock functionality
option(DD_CLOCK_USE_DEFAULT "Use default clock implementation. If disabled, you must supply an implementation of platform/clock.hpp in a file specified as DD_CLOCK_SRC" ON)
if(DD_CLOCK_USE_DEFAULT)
    target_sources(ddsdkcpp PRIVATE datadog/impl/core/platform/clock_std.cpp)
else()
    set(DD_CLOCK_SRC "" CACHE STRING "Path to custom implementation of platform/clock.hpp")
    if(DD_CLOCK_SRC)
        target_sources(ddsdkcpp PRIVATE ${DD_CLOCK_SRC})
    endif()
endif()

# Platform-specific support for system info
option(DD_SYSTEMINFO_USE_DEFAULT "Use default system info implementation. If disabled, you must supply an implementation of platform/system_info.hpp in a file specified as DD_SYSTEMINFO_SRC" ON)
if(DD_SYSTEMINFO_USE_DEFAULT)
    if(WIN32)
        target_sources(ddsdkcpp PRIVATE datadog/impl/core/platform/system_info_windows.cpp)
    elseif(APPLE)
        target_sources(ddsdkcpp PRIVATE datadog/impl/core/platform/system_info_macos.cpp)
        target_link_libraries(ddsdkcpp PRIVATE "-framework CoreFoundation")
    elseif(UNIX)
        target_sources(ddsdkcpp PRIVATE datadog/impl/core/platform/system_info_linux.cpp)
    endif()
else()
    set(DD_SYSTEMINFO_SRC "" CACHE STRING "Path to custom implementation of platform/system_info.hpp")
    if(DD_SYSTEMINFO_SRC)
        target_sources(ddsdkcpp PRIVATE ${DD_SYSTEMINFO_SRC})
    endif()
endif()

# === logging ===

target_sources(ddsdkcpp PRIVATE
    datadog/impl/logging/event_generation.cpp
    datadog/impl/logging/logger.cpp
    datadog/impl/logging/logging.cpp
    datadog/impl/logging/tags.cpp
)

# === rum ===

target_sources(ddsdkcpp PRIVATE
    datadog/impl/rum/containers/resource_map.cpp
    datadog/impl/rum/containers/view_array.cpp
    datadog/impl/rum/crash_processing/crash_formatting.cpp
    datadog/impl/rum/crash_processing/crash_handling.cpp
    datadog/impl/rum/crash_processing/json_scanner.cpp
    datadog/impl/rum/crash_processing/view_event_mutation.cpp
    datadog/impl/rum/crash_processing/view_event_parser.cpp
    datadog/impl/rum/scopes/action.cpp
    datadog/impl/rum/scopes/application.cpp
    datadog/impl/rum/scopes/resource.cpp
    datadog/impl/rum/scopes/session.cpp
    datadog/impl/rum/scopes/view.cpp
    datadog/impl/rum/context.cpp
    datadog/impl/rum/rum.cpp
    datadog/impl/rum/scope.cpp
)

# === crash_reporting ===

target_sources(ddsdkcpp PRIVATE
    datadog/impl/crash_reporting/data/crash_context_read.cpp
    datadog/impl/crash_reporting/data/crash_report_read.cpp
    datadog/impl/crash_reporting/crash_handler_init.cpp
    datadog/impl/crash_reporting/crash_processing.cpp
    datadog/impl/crash_reporting/crash_reporting.cpp
)

if(DD_CRASH_MODE STREQUAL "noop")
    # No-op crash handler implementation requires no additional code
    target_sources(ddsdkcpp PRIVATE
        datadog/impl/crash_reporting/handlers/noop/crash_handler_noop.cpp
    )
elseif(DD_CRASH_MODE STREQUAL "inprocess")
    # In-process crash handler needs to write binary crash report files
    target_sources(ddsdkcpp PRIVATE
        datadog/impl/crash_reporting/data/crash_context_write.cpp
        datadog/impl/crash_reporting/data/crash_report_write.cpp
    )

    # In-process crash handler needs to cache module build IDs, in addition to compiling
    # a platform-specific ICrashHandler implementation (POSIX or Windows)
    target_sources(ddsdkcpp PRIVATE
        datadog/impl/crash_reporting/handlers/inprocess/buildid_cache.cpp
    )
    if(WIN32)
        target_sources(ddsdkcpp PRIVATE
            datadog/impl/crash_reporting/handlers/inprocess/crash_handler_inprocess_windows.cpp
        )
    elseif(UNIX)
        target_sources(ddsdkcpp PRIVATE
            datadog/impl/crash_reporting/handlers/inprocess/crash_handler_inprocess_posix.cpp
        )
        # POSIX stack-walking currently depends on intact frame pointers
        target_compile_options(ddsdkcpp PRIVATE -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "In-process crash handler not supported on this platform")
    endif()
elseif(DD_CRASH_MODE STREQUAL "crashpad")
    # If building with crashpad support, integrate crashpad as a dependency: this
    # adds an ExternalProject target that will build crashpad, then adds the
    # resulting static lib as a dependency for ddsdkcpp. To ensure
    # binary-compatible builds, this script must run after compile/link options for
    # ddsdkcpp are fully configured.
    include(${DD_SDK_ROOT_DIR}/cmake/crashpad.cmake)
    target_sources(ddsdkcpp PRIVATE
        datadog/impl/crash_reporting/handlers/crashpad/crash_handler_crashpad.cpp
    )
else()
    message(FATAL_ERROR "Invalid DD_CRASH_MODE: ${DD_CRASH_MODE}. Must be 'noop', 'inprocess', or 'crashpad'")
endif()

# Install configuration
if(DD_BUILD_INSTALL)
    install(TARGETS ddsdkcpp
        EXPORT DatadogTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()
