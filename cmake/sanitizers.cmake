# Enable sanitizers for a given target based on DD_ENABLE_SANITIZERS list
function(target_enable_sanitizers target_name)
    if(DD_ENABLE_SANITIZERS)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
            # If DD_ENABLE_SANITIZERS=ON, default to 'ASan,UBSan'
            if(DD_ENABLE_SANITIZERS STREQUAL "ON" OR DD_ENABLE_SANITIZERS STREQUAL "TRUE")
                set(DD_ENABLE_SANITIZERS "ASan,UBSan")
            endif()
            
            # Parse the sanitizers list
            string(REPLACE "," ";" SANITIZER_LIST "${DD_ENABLE_SANITIZERS}")

            # Check for incompatible combinations (UBSan is fine with all)
            list(FIND SANITIZER_LIST "TSan" TSAN_FOUND)
            list(FIND SANITIZER_LIST "ASan" ASAN_FOUND)
            list(FIND SANITIZER_LIST "MSan" MSAN_FOUND)
            if((NOT TSAN_FOUND EQUAL -1) AND ((NOT ASAN_FOUND EQUAL -1) OR (NOT MSAN_FOUND EQUAL -1)))
                message(FATAL_ERROR "ThreadSanitizer (TSan) cannot be used with AddressSanitizer (ASan) or MemorySanitizer (MSan)")
            endif()
            if((NOT ASAN_FOUND EQUAL -1) AND (NOT MSAN_FOUND EQUAL -1))
                message(FATAL_ERROR "AddressSanitizer (ASan) cannot be used with MemorySanitizer (MSan)")
            endif()
            
            # Add flags for each requested sanitizer
            set(SANITIZE_FLAGS "")
            set(COMPILE_FLAGS "")
            foreach(sanitizer IN LISTS SANITIZER_LIST)
                if(sanitizer STREQUAL "ASan")
                    list(APPEND SANITIZE_FLAGS "address")
                    list(APPEND COMPILE_FLAGS "-fno-omit-frame-pointer")
                elseif(sanitizer STREQUAL "UBSan")
                    list(APPEND SANITIZE_FLAGS "undefined")
                    list(APPEND COMPILE_FLAGS "-fno-sanitize-recover=all")
                elseif(sanitizer STREQUAL "TSan")
                    list(APPEND SANITIZE_FLAGS "thread")
                    list(APPEND COMPILE_FLAGS "-fno-omit-frame-pointer" "-g")
                elseif(sanitizer STREQUAL "MSan")
                    list(APPEND SANITIZE_FLAGS "memory")
                    list(APPEND COMPILE_FLAGS "-fno-omit-frame-pointer" "-fsanitize-memory-track-origins")
                else()
                    message(WARNING "Unknown sanitizer: ${sanitizer}")
                endif()
            endforeach()
            
            if(SANITIZE_FLAGS)
                string(REPLACE ";" "," SANITIZE_FLAGS_STR "${SANITIZE_FLAGS}")
                target_compile_options(${target_name} PRIVATE
                    -fsanitize=${SANITIZE_FLAGS_STR}
                    ${COMPILE_FLAGS}
                )
                target_link_options(${target_name} PRIVATE
                    -fsanitize=${SANITIZE_FLAGS_STR}
                )
            endif()
        endif()
    endif()
endfunction()
