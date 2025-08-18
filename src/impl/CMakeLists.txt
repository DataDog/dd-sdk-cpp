# Internal library implementation, used with both C and C++ APIs
add_library(dd_native_impl STATIC
    # TODO: src/impl TUs not in this PR
)
target_enable_strict_warnings(dd_native_impl)
target_enable_clang_tidy(dd_native_impl)
target_enable_coverage(dd_native_impl)

target_include_directories(dd_native_impl
    PUBLIC ${CMAKE_SOURCE_DIR}/include-c
    PUBLIC ${CMAKE_SOURCE_DIR}/include-cpp
    PRIVATE .
)

# expected-lite is a header-only implementation of C++23's std::expected - add its
# includes as system headers so they're not subject to strict project-level warnings
include(${CMAKE_SOURCE_DIR}/cmake/deps/expected-lite.cmake)
target_link_libraries(dd_native_impl PUBLIC nonstd::expected-lite)
get_target_property(EXPECTED_LITE_INCLUDE_DIRS nonstd::expected-lite INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(dd_native_impl SYSTEM PUBLIC ${EXPECTED_LITE_INCLUDE_DIRS})

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
