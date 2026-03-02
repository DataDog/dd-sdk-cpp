include(FetchContent)

# Disable Catch2 docs, tests, and examples
set(CATCH_INSTALL_DOCS OFF)
set(CATCH_INSTALL_HELPERS OFF)
set(CATCH_BUILD_TESTING OFF)
set(CATCH_BUILD_EXAMPLES OFF)
set(CATCH_BUILD_EXTRA_TESTS OFF)
set(CATCH_BUILD_FUZZERS OFF)

FetchContent_Declare(
    Catch2
    URL "https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.0.tar.gz"
    URL_MD5 "e06399e3f661b72080a0eabe9643b461"
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(Catch2)

# Mark Catch2 includes as SYSTEM to suppress warnings from its headers
set_target_properties(Catch2 PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:Catch2,INTERFACE_INCLUDE_DIRECTORIES>
)
set_target_properties(Catch2WithMain PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES $<TARGET_PROPERTY:Catch2WithMain,INTERFACE_INCLUDE_DIRECTORIES>
)
