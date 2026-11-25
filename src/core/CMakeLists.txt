# core: the SDK's runtime, shared by every feature: configuration, the context thread
# and message bus, feature registration, storage, upload, and platform support.
dd_add_module(core
    DEPENDS types
    PUBLIC_HEADERS core
    SOURCES
        datadog/c/core_c_api.cpp
        datadog/cpp/core_cpp_api.cpp
        datadog/impl/core/attribute/binary.cpp
        datadog/impl/core/tracing/b3_header_writer.cpp
        datadog/impl/core/tracing/baggage_header_writer.cpp
        datadog/impl/core/tracing/datadog_header_writer.cpp
        datadog/impl/core/tracing/first_party_hosts_matcher.cpp
        datadog/impl/core/tracing/http_header_writer.cpp
        datadog/impl/core/tracing/span_id_generator.cpp
        datadog/impl/core/tracing/trace_id_generator.cpp
        datadog/impl/core/tracing/w3c_header_writer.cpp
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
        datadog/impl/core/types.cpp
        datadog/impl/core/upload_scheduler.cpp
        datadog/impl/core/upload_thread.cpp
)

# The SDK runs its work on background threads. Before glibc 2.34, pthread functions live
# in libpthread rather than libc, so link the platform's thread library explicitly: on
# newer glibc, and on macOS and Windows, this adds nothing to the link.
find_package(Threads REQUIRED)
target_link_libraries(ddsdkcpp_core PRIVATE Threads::Threads)

# core/storage, with IFilesystem implementation determined per-platform
target_sources(ddsdkcpp_core PRIVATE
    datadog/impl/core/storage/artifact.cpp
    datadog/impl/core/storage/feature_event.cpp
    datadog/impl/core/storage/filesystem_wrapper.cpp
    datadog/impl/core/storage/path.cpp
    datadog/impl/core/storage/sdk.cpp
    datadog/impl/core/storage/util.cpp
)
if(WIN32)
    target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/storage/filesystem_windows.cpp)
else()
    target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/storage/filesystem_posix.cpp)
endif()

# core/http, with IHttpClient implementation using libcurl on POSIX, WinHTTP on Win32
target_sources(ddsdkcpp_core PRIVATE
    datadog/impl/core/http/body_writer_multipart.cpp
    datadog/impl/core/http/body_writer_string.cpp
    datadog/impl/core/http/body_writer_tlv.cpp
    datadog/impl/core/http/request_builder.cpp
)

# We support different HTTP client implementations:
# - DD_HTTP_CLIENT=winhttp (default on Windows; uses the WinHTTP API built into the OS)
# - DD_HTTP_CLIENT=libcurl (default elsewhere)
# - DD_HTTP_CLIENT=none    (define no client; for use by cross-platform SDKs)
if(WIN32)
    set(DD_HTTP_CLIENT_DEFAULT "winhttp")
else()
    set(DD_HTTP_CLIENT_DEFAULT "libcurl")
endif()
set(DD_HTTP_CLIENT "${DD_HTTP_CLIENT_DEFAULT}" CACHE STRING "HTTP client backend (winhttp, libcurl, none)")
set_property(CACHE DD_HTTP_CLIENT PROPERTY STRINGS "winhttp" "libcurl" "none")

if(DD_HTTP_CLIENT STREQUAL "winhttp")
    if(NOT WIN32)
        message(FATAL_ERROR "DD_HTTP_CLIENT=winhttp is only supported on Windows")
    endif()

    # Use the internal IHttpClient implementation that depends on WinHTTP, which is
    # installed with the operating system: nothing needs to be built or vendored
    target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/http/client_winhttp.cpp)
    target_link_libraries(ddsdkcpp_core PRIVATE winhttp)
elseif(DD_HTTP_CLIENT STREQUAL "libcurl")
    # Use the internal IHttpClient implementation that depends on libcurl
    target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/http/client_curl.cpp)

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
    target_link_libraries(ddsdkcpp_core PRIVATE CURL::libcurl)
    get_target_property(CURL_INCLUDE_DIRS CURL::libcurl INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(ddsdkcpp_core SYSTEM PRIVATE ${CURL_INCLUDE_DIRS})
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
    message(FATAL_ERROR "Invalid DD_HTTP_CLIENT: ${DD_HTTP_CLIENT}. Must be 'winhttp', 'libcurl', or 'none'")
endif()

# Platform-specific support for system clock functionality
option(DD_CLOCK_USE_DEFAULT "Use default clock implementation. If disabled, you must supply an implementation of platform/clock.hpp in a file specified as DD_CLOCK_SRC" ON)
if(DD_CLOCK_USE_DEFAULT)
    target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/platform/clock_std.cpp)
else()
    set(DD_CLOCK_SRC "" CACHE STRING "Path to custom implementation of platform/clock.hpp")
    if(DD_CLOCK_SRC)
        target_sources(ddsdkcpp_core PRIVATE ${DD_CLOCK_SRC})
    endif()
endif()

# Platform-specific support for system info
option(DD_SYSTEMINFO_USE_DEFAULT "Use default system info implementation. If disabled, you must supply an implementation of platform/system_info.hpp in a file specified as DD_SYSTEMINFO_SRC" ON)
if(DD_SYSTEMINFO_USE_DEFAULT)
    if(WIN32)
        target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/platform/system_info_windows.cpp)
    elseif(APPLE)
        target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/platform/system_info_macos.cpp)
        target_link_libraries(ddsdkcpp_core PRIVATE "-framework CoreFoundation")
    elseif(UNIX)
        target_sources(ddsdkcpp_core PRIVATE datadog/impl/core/platform/system_info_linux.cpp)
    endif()
else()
    set(DD_SYSTEMINFO_SRC "" CACHE STRING "Path to custom implementation of platform/system_info.hpp")
    if(DD_SYSTEMINFO_SRC)
        target_sources(ddsdkcpp_core PRIVATE ${DD_SYSTEMINFO_SRC})
    endif()
endif()
