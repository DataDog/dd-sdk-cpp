include(FetchContent)

FetchContent_Declare(
    date_src
    URL "https://github.com/HowardHinnant/date/archive/refs/tags/v3.0.4.tar.gz"
    URL_MD5 "7a716762dcf1f8d8f40ed1aaa207a22e"
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(date_src)
