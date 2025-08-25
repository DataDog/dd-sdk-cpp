add_library(dd_native_cpp_api STATIC
    attribute.cpp
    core.cpp
    logging.cpp
)
target_enable_strict_warnings(dd_native_cpp_api)
target_enable_clang_tidy(dd_native_cpp_api)
target_enable_coverage(dd_native_cpp_api)

target_include_directories(dd_native_cpp_api
    PUBLIC ${CMAKE_SOURCE_DIR}/include-cpp
    PRIVATE ${CMAKE_SOURCE_DIR}/src/impl
)

target_link_libraries(dd_native_cpp_api PRIVATE dd_native_impl)
