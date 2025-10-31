# nlohmann/json is a single-header C++11 JSON library: we use it as a development-only
# dependency to validate the JSON objects produced by our SDK's own JSON serialization
# implementation
set(NLOHMANN_JSON_TAG "v3.12.0")
set(NLOHMANN_JSON_HEADER_LOCAL "${EXTERNAL_ROOT}/include/nlohmann/json.hpp")
set(NLOHMANN_JSON_HEADER_URL "https://github.com/nlohmann/json/releases/download/${NLOHMANN_JSON_TAG}/json.hpp")
set(NLOHMANN_JSON_HEADER_MD5 "57678c38c69af31d32a6ae56a9cf9548")

set(NLOHMANN_JSON_CURRENT_MD5 "")
if(EXISTS "${NLOHMANN_JSON_HEADER_LOCAL}")
    file(MD5 "${NLOHMANN_JSON_HEADER_LOCAL}" NLOHMANN_JSON_CURRENT_MD5)
endif()

if("${NLOHMANN_JSON_CURRENT_MD5}" STREQUAL "${NLOHMANN_JSON_HEADER_MD5}")
    message(STATUS "nlohmann/json.hpp found: ${NLOHMANN_JSON_HEADER_LOCAL}")
else()
    message(STATUS "Fetching nlohmann/json.hpp @ ${NLOHMANN_JSON_TAG}")
    file(DOWNLOAD
        "${NLOHMANN_JSON_HEADER_URL}"
        "${NLOHMANN_JSON_HEADER_LOCAL}"
        TLS_VERIFY ON
        EXPECTED_MD5 "${NLOHMANN_JSON_HEADER_MD5}"
    )
    message(STATUS "nlohmann/json.hpp added: ${NLOHMANN_JSON_HEADER_LOCAL}")
endif()
