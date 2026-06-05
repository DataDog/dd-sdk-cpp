# This file defines convenience functions that aim to streamline the CMake configuration
# required in order to add the Datadog C++ SDK as a dependency for an executable target,
# configured as needed based on the set of features and options used to build the SDK.

# datadog_enable(my-app):
#
# - adds the Datadog SDK as a linker dependency for my-app, allowing you to call SDK
#   functions.
# - (if Crashpad support is enabled): copies the crashpad_handler executable alongside
#   the binary for my-app, ensuring that crash reporting will work without explicitly
#   configuring a handler path in development builds of my-app.
#
# Call this function after including the SDK via FetchContent or find_package(Datadog).
# If you need finer-grained control, link against `Datadog::sdk` directly instead.
#
function(datadog_enable target)
    # Require that the target exists
    if(NOT TARGET ${target})
        message(FATAL_ERROR "datadog_enable(): target '${target}' does not exist")
    endif()

    # Add the core Datadog SDK library module as a dependency, allowing builds of this
    # target to a.) resolve #include "datadog.hpp" etc. to the SDK's include/ dir, and
    # b.) link against all required libs in the SDK's lib/ dir
    target_link_libraries(${target} PRIVATE Datadog::sdk)

    # If the SDK was built with crashpad support, ensure that the crashpad_handler
    # executable is present alongside the application binary post-build
    if((DD_CRASH_MODE STREQUAL "crashpad") OR (DATADOG_BUILT_WITH_DD_CRASH_MODE STREQUAL "crashpad"))
        if (TARGET crashpad::handler)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE:crashpad::handler>"
                        "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
                COMMENT "Copying crashpad_handler executable to $<TARGET_FILE_DIR:${target}>"
            )
        else()
            message(FATAL_ERROR "datadog_enable(): target crashpad::handler does not exist")
        endif()
    endif()
endfunction()

# datadog_install(bin):
#
# - (if Crashpad support is enabled) ensures that the crashpad_handler executable will
#   be copied to bin/ alongside your application
#
# Call this function after defining `install(TARGETS my-app ...)` for your app,
# specifying the destination directory for your application's binaries in lieu of `bin`.
# You may elect not to call this function if a.) your project does not use CMake
# installation rules, b.) you don't need Crashpad support, or c.) you are ensuring that
# the crashpad_handler exectuable makes its way into your builds through other means.
#
function(datadog_install destination)
    # If the SDK was built with crashpad support, install the crashpad_handler
    # executable to the destination directory
    if((DD_CRASH_MODE STREQUAL "crashpad") OR (DATADOG_BUILT_WITH_DD_CRASH_MODE STREQUAL "crashpad"))
        if(TARGET crashpad::handler)
            get_target_property(CRASHPAD_HANDLER_PATH crashpad::handler IMPORTED_LOCATION)
            install(PROGRAMS ${CRASHPAD_HANDLER_PATH} DESTINATION ${destination})
        else()
            message(FATAL_ERROR "datadog_install(): target crashpad::handler does not exist")
        endif()
    endif()
endfunction()
