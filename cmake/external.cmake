# This file manages single-file header-only libraries that are internal to the library
# implementation
set(EXTERNAL_ROOT "${CMAKE_CURRENT_BINARY_DIR}/external")
set(EXTERNAL_INCLUDE "${EXTERNAL_ROOT}/include")

include(${CMAKE_CURRENT_LIST_DIR}/deps/date.cmake)

function(target_include_external_deps target_name)
    target_include_directories(${target_name} SYSTEM PRIVATE ${EXTERNAL_INCLUDE})
endfunction()
