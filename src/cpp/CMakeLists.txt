add_library(dd_native_cpp_api STATIC
    core.cpp
    logging.cpp
)

target_include_directories(dd_native_cpp_api
    PUBLIC ${CMAKE_SOURCE_DIR}/include-cpp
    PRIVATE ${CMAKE_SOURCE_DIR}/src/impl
)

target_link_libraries(dd_native_cpp_api PRIVATE dd_native_impl)
