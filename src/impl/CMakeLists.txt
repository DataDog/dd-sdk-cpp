option(DD_CLOCK_USE_DEFAULT "Use default clock implementation. If disabled, you must supply an implementation of platform/clock.hpp in a file specified as DD_CLOCK_SRC" ON)
if(DD_CLOCK_USE_DEFAULT)
    set(DD_CLOCK_SRC platform/clock_default.cpp)
else()
    set(DD_CLOCK_SRC "" CACHE STRING "Path to custom implementation of platform/clock.hpp")
endif()

option(DD_HTTP_USE_DEFAULT "Use default HTTP client implementation. If disabled, you must supply an implementation of platform/http.hpp in a file specified as DD_HTTP_SRC" ON)
if(DD_HTTP_USE_DEFAULT)
    include(${CMAKE_SOURCE_DIR}/cmake/deps/libcurl.cmake)
    set(LIBCURL_STATIC CURL::libcurl)
    set(DD_HTTP_SRC platform/http_curl.cpp)
else()
    set(DD_HTTP_SRC "" CACHE STRING "Path to custom implementation of platform/http.hpp")
endif()

add_library(dd_native_impl STATIC
    core/core.cpp
    logging/logging.cpp
    ${DD_CLOCK_SRC}
    ${DD_HTTP_SRC}
)

target_link_libraries(dd_native_impl PRIVATE 
  ${LIBCURL_STATIC}
)

target_include_directories(dd_native_impl
    PUBLIC ${CMAKE_SOURCE_DIR}/include-c
    PUBLIC ${CMAKE_SOURCE_DIR}/include-cpp
    PUBLIC .
)
