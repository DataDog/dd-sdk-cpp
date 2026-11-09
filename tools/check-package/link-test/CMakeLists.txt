# Links every object in a packaged static build of dd-sdk-cpp into a shared library,
# using the package's own DatadogConfig.cmake to resolve its link dependencies. The link
# fails if the library references any symbol that neither it nor those dependencies
# define. tools/check-package/main.py configures this project with the same toolchain
# file as the package build, then inspects the resulting shared library.
#
# $<LINK_LIBRARY:WHOLE_ARCHIVE,...> requires CMake 3.24
cmake_minimum_required(VERSION 3.24)
project(dd_sdk_cpp_link_test CXX)

# Datadog_DIR must point to the package's lib/cmake/Datadog directory
find_package(Datadog REQUIRED CONFIG)

add_library(link_test SHARED link_test.cpp)
target_link_libraries(link_test PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,Datadog::sdk>")
target_link_options(link_test PRIVATE "LINKER:--no-undefined")
