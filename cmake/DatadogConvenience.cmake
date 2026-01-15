# Convenience function to enable the Datadog SDK for a given executable target
function(datadog_enable target)
    # Require that the target exists
    if(NOT TARGET ${target})
        message(FATAL_ERROR "datadog_enable(): target '${target}' does not exist")
    endif()

    # Add the core Datadog SDK library module as a dependency, allowing builds of this
    # target to a.) resolve #include "datadog.hpp" etc. to the SDK's include/ dir, and
    # b.) link against all required libs in the SDK's lib/ dir
    target_link_libraries(${target} PRIVATE Datadog::dd_native)

    # If the SDK was built with crashpad support, ensure that the crashpad_handler
    # executable is present alongside the application binary post-build
    if(DD_ENABLE_CRASHPAD OR DATADOG_WITH_CRASHPAD)
        if (TARGET crashpad::handler)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:crashpad::handler>"
                        "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
                COMMENT "Copying crashpad_handler executable to $<TARGET_FILE_DIR:${target}>"
            )
        else()
            message(FATAL_ERROR "Target crashpad::handler does not exist")
        endif()
    endif()
endfunction()
