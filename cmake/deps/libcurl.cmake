include(FetchContent)

# No need to build curl executable
set(BUILD_CURL_EXE OFF)
set(BUILD_LIBCURL_DOCS OFF)
set(BUILD_MISC_DOCS OFF)
set(ENABLE_CURL_MANUAL OFF)

# Disable curl features
set(CURL_BROTLI OFF)
set(CURL_USE_LIBSSH2 OFF)
set(CURL_USE_LIBPSL OFF)
set(CURL_DISABLE_INSTALL ON)
set(CURL_DISABLE_ALTSVC ON)
set(CURL_DISABLE_SRP ON)
set(CURL_DISABLE_LDAP ON)
set(USE_LIBIDN2 OFF)
set(CURL_ENABLE_EXPORT_TARGET OFF)

# Enable SSL/TLS support
if(WIN32)
    # Use Windows native SSL (Schannel) - no external dependencies,
    # automatically uses Windows certificate store
    set(CURL_USE_SCHANNEL ON)
else()
    # On Unix systems, let curl auto-detect OpenSSL or other system SSL
    # libraries (works reliably on macOS and Linux)
    set(CURL_USE_OPENSSL ON)
endif()

# Disable building examples for CURL but don't pollute other portions
set(_original_build_examples ${BUILD_EXAMPLES})
set(BUILD_EXAMPLES OFF)

# Force static library build - save original value and restore after
set(_original_build_shared_libs ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
set(CURL_STATICLIB ON)

# Enable compiled object files to be shared across targets
set(SHARE_LIB_OBJECT ON)

FetchContent_Declare(
    curl
    URL "https://github.com/curl/curl/releases/download/curl-8_11_0/curl-8.11.0.tar.gz"
    URL_MD5 "7b6b3613ed9be7b6c010e0750b2c48bb"
)
FetchContent_MakeAvailable(curl)

# Restore original BUILD_SHARED_LIBS and BUILD_EXAMPLES setting
set(BUILD_SHARED_LIBS ${_original_build_shared_libs})
set(BUILD_EXAMPLES ${_original_build_examples})

# Make libcurl exportable by adding it to the export set (only when installing)
if(DD_BUILD_INSTALL)
    install(TARGETS libcurl_static
        EXPORT DatadogTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()
