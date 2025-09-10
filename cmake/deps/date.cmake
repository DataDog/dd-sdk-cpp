# HowardHinnant/date provides std::chrono-compatible, C++20-compliant, datetime
# handling, which we use primarily to format ISO timestamps
set(HHINNANT_DATE_TAG "v3.0.4")
set(HHINNANT_DATE_HEADER_PATH "include/date/date.h")
set(HHINNANT_DATE_HEADER_LOCAL "${EXTERNAL_ROOT}/${HHINNANT_DATE_HEADER_PATH}")
set(HHINNANT_DATE_HEADER_MD5 "365a61623a396f7a1b47964b237f304a")

set(HHINNANT_DATE_CURRENT_MD5 "")
if(EXISTS "${HHINNANT_DATE_HEADER_LOCAL}")
    file(MD5 "${HHINNANT_DATE_HEADER_LOCAL}" HHINNANT_DATE_CURRENT_MD5)
endif()

if("${HHINNANT_DATE_CURRENT_MD5}" STREQUAL "${HHINNANT_DATE_HEADER_MD5}")
    message(STATUS "date.h found: ${HHINNANT_DATE_HEADER_LOCAL}")
else()
    message(STATUS "Fetching date.h @ ${HHINNANT_DATE_TAG}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/HowardHinnant/date/refs/tags/${HHINNANT_DATE_TAG}/${HHINNANT_DATE_HEADER_PATH}"
        "${HHINNANT_DATE_HEADER_LOCAL}"
        TLS_VERIFY ON
        EXPECTED_MD5 "${HHINNANT_DATE_HEADER_MD5}"
    )
    message(STATUS "date.h added: ${HHINNANT_DATE_HEADER_LOCAL}")
endif()
