include(${CMAKE_SOURCE_DIR}/cmake/deps/catch2.cmake)

# Create test executable
add_executable(dd_tests
    example_test.cpp
)

target_link_libraries(dd_tests 
    PRIVATE 
    Catch2::Catch2WithMain
)

# Register tests with CTest
include(CTest)
include(Catch)
catch_discover_tests(dd_tests)
