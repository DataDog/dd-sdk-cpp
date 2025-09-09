# Explicitly enable strict warnings on a per-target basis
function(target_enable_strict_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

# If configured with asserts enabled, define WITH_DATADOG_ASSERTS for targets that call
# DATADOG_ASSERT
function(target_enable_datadog_assert target_name)
    if(DD_ENABLE_ASSERTS)
        target_compile_definitions(${target_name} PRIVATE WITH_DATADOG_ASSERTS=1)
    endif()
endfunction()
