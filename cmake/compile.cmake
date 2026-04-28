# Explicitly enable strict warnings on a per-target basis
function(target_enable_strict_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(${target_name} PRIVATE /W4 /WX /utf-8
            /external:anglebrackets /external:W0)
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

# Configure shared library support for a target
function(target_enable_shared_library_support target_name)
    set_target_properties(${target_name} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    if(DD_BUILD_SHARED)
        target_compile_definitions(${target_name}
            PUBLIC DATADOG_SHARED_LIB
            PRIVATE WITH_DATADOG_EXPORTS
        )
    endif()
endfunction()
