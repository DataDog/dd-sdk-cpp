include(${CMAKE_SOURCE_DIR}/cmake/deps/catch2.cmake)

# Create test executable
add_executable(dd_tests
    impl/core/block_test.cpp
    impl/core/context_test.cpp
    impl/core/queue_test.cpp
    impl/core/site_test.cpp
)

target_link_libraries(dd_tests
    PRIVATE
    dd_native_impl
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(dd_tests)
