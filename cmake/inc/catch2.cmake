Include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG ${CATCH2_COMMIT_HASH}
)

FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
set_target_properties(
    Catch2
    PROPERTIES
        CMAKE_CXX_CLANG_TIDY ""
)

include(CTest)
include(Catch)
