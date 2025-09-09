# Enable sanitizers for a given target, if configured and supported
function(target_enable_sanitizers target_name)
    if(DD_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            target_compile_options(${target_name} PRIVATE
                -fsanitize=address
                -fsanitize=undefined
                -fno-sanitize-recover=all
                -fno-omit-frame-pointer
            )
            target_link_options(${target_name} PRIVATE
                -fsanitize=address
                -fsanitize=undefined
            )
        endif()
    endif()
endfunction()
