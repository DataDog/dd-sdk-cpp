# nonstd/expected-lite is a header-only implementation of C++23's std::expected
set(EXPECTED_LITE_TAG "v0.9.0")
set(EXPECTED_LITE_HEADER_PATH "include/nonstd/expected.hpp")
set(EXPECTED_LITE_HEADER_LOCAL "${EXTERNAL_ROOT}/${EXPECTED_LITE_HEADER_PATH}")
set(EXPECTED_LITE_HEADER_MD5 "566ee4a5c287621edf0c12c342780b7d")

set(EXPECTED_LITE_CURRENT_MD5 "")
if(EXISTS "${EXPECTED_LITE_HEADER_LOCAL}")
    file(MD5 "${EXPECTED_LITE_HEADER_LOCAL}" EXPECTED_LITE_CURRENT_MD5)
endif()

if("${EXPECTED_LITE_CURRENT_MD5}" STREQUAL "${EXPECTED_LITE_HEADER_MD5}")
    message(STATUS "expected.hpp found: ${EXPECTED_LITE_HEADER_LOCAL}")
else()
    message(STATUS "Fetching expected.hpp @ ${EXPECTED_LITE_TAG}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/martinmoene/expected-lite/refs/tags/${EXPECTED_LITE_TAG}/${EXPECTED_LITE_HEADER_PATH}"
        "${EXPECTED_LITE_HEADER_LOCAL}"
        TLS_VERIFY ON
        EXPECTED_MD5 "${EXPECTED_LITE_HEADER_MD5}"
    )
    message(STATUS "expected.hpp added: ${EXPECTED_LITE_HEADER_LOCAL}")
endif()
