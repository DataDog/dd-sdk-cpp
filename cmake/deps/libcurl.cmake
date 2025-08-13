include(FetchContent)

# No need to build curl executable
set(BUILD_CURL_EXE OFF)
set(BUILD_LIBCURL_DOCS OFF)
set(BUILD_MISC_DOCS OFF)

# Disable curl features
set(CURL_BROTLI OFF)
set(CURL_USE_LIBSSH2 OFF)
set(CURL_USE_LIBPSL OFF)
set(CURL_DISABLE_INSTALL ON)
set(CURL_DISABLE_ALTSVC ON)
set(CURL_DISABLE_SRP ON)
set(CURL_DISABLE_LDAP ON)
set(USE_LIBIDN2 OFF)

# Force static library build
set(BUILD_SHARED_LIBS OFF)
set(CURL_STATICLIB ON)

# Enable compiled object files to be shared across targets
set(SHARE_LIB_OBJECT ON)

FetchContent_Declare(
    curl
    URL "https://github.com/curl/curl/releases/download/curl-8_11_0/curl-8.11.0.tar.gz"
    URL_MD5 "7b6b3613ed9be7b6c010e0750b2c48bb"
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(curl)
