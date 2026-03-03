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
# If you need finer-grained control, link against `Datadog::dd_native` directly instead.
#
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

    # If the SDK was built with profiler support, copy the required runtime DLLs
    # (dd-win-prof.dll and datadog_profiling_ffi.dll) alongside the application
    # binary so they can be found at runtime.
    if(DD_ENABLE_PROFILER OR DATADOG_BUILT_WITH_DD_ENABLE_PROFILER)
        if(COMMAND dd_win_prof_copy_runtime_deps)
            dd_win_prof_copy_runtime_deps(${target})
        else()
            message(FATAL_ERROR
                "datadog_enable(): dd_win_prof_copy_runtime_deps is not available; "
                "ensure dd-win-prof was fetched before calling datadog_enable()")
        endif()
    endif()
endfunction()

# datadog_install(destination):
#
# Installs runtime dependencies required by the SDK alongside your application:
# - (if Crashpad support is enabled) copies the crashpad_handler executable
# - (if Profiler support is enabled) copies dd-win-prof.dll and datadog_profiling_ffi.dll
#
# Call this function after defining `install(TARGETS my-app ...)` for your app,
# specifying the destination directory for your application's binaries (typically "bin").
# You may elect not to call this function if: a.) your project does not use CMake
# installation rules, b.) you don't need Crashpad or Profiler support, or c.) you are
# handling runtime dependencies through other means.
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

    # If the SDK was built with profiler support, install the required runtime DLLs
    # to the destination directory
    if((DD_ENABLE_PROFILER) OR (DATADOG_BUILT_WITH_DD_ENABLE_PROFILER))
        if(TARGET dd-win-prof)
            # Try to get imported location (for find_package case)
            get_target_property(DD_WIN_PROF_DLL dd-win-prof IMPORTED_LOCATION)

            if(DD_WIN_PROF_DLL AND NOT DD_WIN_PROF_DLL MATCHES "-NOTFOUND$")
                # Imported target case: install the DLL from its imported location
                install(PROGRAMS ${DD_WIN_PROF_DLL} DESTINATION ${destination})

                # Also install datadog_profiling_ffi.dll from the same directory
                get_filename_component(DD_WIN_PROF_DIR ${DD_WIN_PROF_DLL} DIRECTORY)
                install(PROGRAMS "${DD_WIN_PROF_DIR}/datadog_profiling_ffi.dll" DESTINATION ${destination})
            else()
                # FetchContent case: use generator expressions to get target files
                install(FILES "$<TARGET_FILE:dd-win-prof>" DESTINATION ${destination})
                if(TARGET libdatadog_dynamic)
                    install(FILES "$<TARGET_FILE:libdatadog_dynamic>" DESTINATION ${destination})
                endif()
            endif()
        else()
            message(FATAL_ERROR "datadog_install(): target dd-win-prof does not exist")
        endif()
    endif()
endfunction()
