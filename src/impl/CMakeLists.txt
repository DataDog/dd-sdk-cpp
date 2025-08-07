# Internal library implementation, used with both C and C++ APIs
add_library(dd_native_impl STATIC
    core/tlv.cpp
    core/context.cpp
    core/core.cpp
    core/feature.cpp
    core/storage.cpp
    core/upload.cpp
    features/logging/logging.cpp
)
target_enable_strict_warnings(dd_native_impl)

target_include_directories(dd_native_impl
    PUBLIC ${CMAKE_SOURCE_DIR}/include-c
    PUBLIC ${CMAKE_SOURCE_DIR}/include-cpp
    PUBLIC .
)

# expected-lite is a header-only implementation of C++23's std::expected
include(${CMAKE_SOURCE_DIR}/cmake/deps/expected-lite.cmake)
target_link_libraries(dd_native_impl PUBLIC
  nonstd::expected-lite
)

# Platform-specific support for system clock functionality
option(DD_CLOCK_USE_DEFAULT "Use default clock implementation. If disabled, you must supply an implementation of platform/clock.hpp in a file specified as DD_CLOCK_SRC" ON)
if(DD_CLOCK_USE_DEFAULT)
    target_sources(dd_native_impl PRIVATE platform/clock_std.cpp)
else()
    set(DD_CLOCK_SRC "" CACHE STRING "Path to custom implementation of platform/clock.hpp")
    if(DD_CLOCK_SRC)
        target_sources(dd_native_impl PRIVATE ${DD_CLOCK_SRC})
    endif()
endif()

# Platform-specific support for file I/O
option(DD_FILESYSTEM_USE_DEFAULT "Use default filesystem implementation. If disabled, you must supply an implementation of platform/filesystem.hpp in a file specified as DD_FILESYSTEM_SRC" ON)
if(DD_FILESYSTEM_USE_DEFAULT)
    target_sources(dd_native_impl PRIVATE platform/filesystem_std.cpp)
else()
    set(DD_FILESYSTEM_SRC "" CACHE STRING "Path to custom implementation of platform/filesystem.hpp")
    if(DD_FILESYSTEM_SRC)
        target_sources(dd_native_impl PRIVATE ${DD_FILESYSTEM_SRC})
    endif()
endif()

# Platform-specific support for HTTP client functionality
option(DD_HTTP_USE_DEFAULT "Use default HTTP client implementation. If disabled, you must supply an implementation of platform/http.hpp in a file specified as DD_HTTP_SRC" ON)
if(DD_HTTP_USE_DEFAULT)
    # Default implementation uses libcurl
    include(${CMAKE_SOURCE_DIR}/cmake/deps/libcurl.cmake)
    target_link_libraries(dd_native_impl PRIVATE CURL::libcurl)
    target_sources(dd_native_impl PRIVATE platform/http_curl.cpp)
else()
    set(DD_HTTP_SRC "" CACHE STRING "Path to custom implementation of platform/http.hpp")
    if(DD_HTTP_SRC)
        target_sources(dd_native_impl PRIVATE ${DD_HTTP_SRC})
    endif()
endif()
