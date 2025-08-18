include(FetchContent)

FetchContent_Declare(
    expected-lite
    URL "https://github.com/martinmoene/expected-lite/archive/refs/tags/v0.9.0.tar.gz"
    URL_MD5 "081334210456ccbb62ccb2eec4d457e0"
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(expected-lite)
