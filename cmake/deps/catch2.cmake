Include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 31588bb4f56b638dd5afc28d3ebff9b9dcefb88d # Release 3.7.0
)

FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
set_target_properties(
    Catch2
    PROPERTIES
        CMAKE_CXX_CLANG_TIDY ""
)
