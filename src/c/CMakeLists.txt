add_library(dd_native_c_api STATIC
    core.cpp
    logging.cpp
)
target_enable_strict_warnings(dd_native_c_api)

target_include_directories(dd_native_c_api
    PUBLIC ${CMAKE_SOURCE_DIR}/include-c
    PRIVATE ${CMAKE_SOURCE_DIR}/src/impl
)

target_link_libraries(dd_native_c_api PRIVATE dd_native_impl)
