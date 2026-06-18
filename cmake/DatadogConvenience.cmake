# This file defines convenience functions that aim to streamline the CMake configuration
# required in order to add the Datadog C++ SDK as a dependency for an executable target,
# configured as needed based on the set of features and options used to build the SDK.

# datadog_enable(my-app):
#
# - Adds the Datadog SDK as a linker dependency for `my-app`, allowing you to call SDK
#   functions.
# - (if Crashpad support is enabled): copies the crashpad_handler executable alongside
#   the binary for `my-app` as a post-build step, ensuring that crash reporting works
#   without explicitly configuring a handler path in development builds of `my-app`.
#
# Call this function after including the SDK via FetchContent or find_package(Datadog).
# If you need finer-grained control, link against `Datadog::sdk` directly instead.
#
# This function configures the dev-build (in-place) workflow. To configure installation
# rules for packaging your app, also call datadog_install().
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

# datadog_install(my-app [RUNTIME_DESTINATION <dir>] [LIBRARY_DESTINATION <dir>]):
#
# Configures installation rules for Datadog SDK components that must be deployed
# alongside the application executable. Call this after defining `install(TARGETS
# my-app ...)` for your app.
#
# - (if the SDK is built as a shared library): installs the SDK shared library to
#   LIBRARY_DESTINATION and, on POSIX systems, stamps INSTALL_RPATH on the target so
#   the installed executable can locate the shared library at runtime.
# - (if Crashpad support is enabled): installs crashpad_handler to RUNTIME_DESTINATION.
#
# RUNTIME_DESTINATION: directory for executables and, on Windows, DLLs. Defaults to
# CMAKE_INSTALL_BINDIR if defined (e.g. via GNUInstallDirs), otherwise "bin".
#
# LIBRARY_DESTINATION: directory for shared libraries (.so/.dylib). Defaults to
# CMAKE_INSTALL_LIBDIR if defined (e.g. via GNUInstallDirs), otherwise "lib". On
# Windows, DLLs are RUNTIME artifacts in CMake's model and always land in
# RUNTIME_DESTINATION regardless of this value. Ignored for static builds.
#
# If INSTALL_RPATH is already set on the target, datadog_install() will not overwrite
# it, allowing you to manage RPATH yourself when needed.
#
# You may elect not to call this function if you prefer to explicitly control how the
# SDK is integrated into your project's build.
#
function(datadog_install target)
    cmake_parse_arguments(ARG "" "RUNTIME_DESTINATION;LIBRARY_DESTINATION" "" ${ARGN})

    # Require that the target exists
    if(NOT TARGET ${target})
        message(FATAL_ERROR "datadog_install(): target '${target}' does not exist")
    endif()

    # Resolve RUNTIME_DESTINATION, preferring CMAKE_INSTALL_BINDIR if defined
    if(NOT ARG_RUNTIME_DESTINATION)
        if(DEFINED CMAKE_INSTALL_BINDIR)
            set(ARG_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
        else()
            set(ARG_RUNTIME_DESTINATION "bin")
        endif()
    endif()

    # Resolve LIBRARY_DESTINATION. On Windows, CMake classifies DLLs as RUNTIME
    # artifacts (not LIBRARY), so LIBRARY_DESTINATION has no effect for DLLs —
    # they always land in RUNTIME_DESTINATION regardless.
    if(NOT ARG_LIBRARY_DESTINATION)
        if(DEFINED CMAKE_INSTALL_LIBDIR)
            set(ARG_LIBRARY_DESTINATION "${CMAKE_INSTALL_LIBDIR}")
        else()
            set(ARG_LIBRARY_DESTINATION "lib")
        endif()
    endif()

    # Determine whether the SDK was built as a shared library, either in this build
    # or as a pre-built package consumed via find_package
    set(_dd_is_shared OFF)
    if(DD_BUILD_SHARED OR DATADOG_BUILT_WITH_DD_BUILD_SHARED)
        set(_dd_is_shared ON)
    endif()

    if(_dd_is_shared)
        # When consumed via FetchContent, ddsdkcpp is the real (non-imported) library
        # target. When consumed via find_package, Datadog::sdk is an IMPORTED target.
        if(TARGET ddsdkcpp)
            install(TARGETS ddsdkcpp
                LIBRARY DESTINATION "${ARG_LIBRARY_DESTINATION}"
                RUNTIME DESTINATION "${ARG_RUNTIME_DESTINATION}"
            )
        else()
            install(IMPORTED_RUNTIME_ARTIFACTS Datadog::sdk
                LIBRARY DESTINATION "${ARG_LIBRARY_DESTINATION}"
                RUNTIME DESTINATION "${ARG_RUNTIME_DESTINATION}"
            )
        endif()

        # On POSIX, stamp INSTALL_RPATH on the app target so it can locate the shared
        # library relative to its own location in the install tree. Skip this if the
        # project has already configured INSTALL_RPATH itself.
        if(NOT WIN32)
            get_target_property(_existing_rpath "${target}" INSTALL_RPATH)
            if(NOT _existing_rpath)
                # Compute the path from the runtime directory to the library directory.
                # Prepending "/" makes these relative paths suitable for file(RELATIVE_PATH),
                # which requires absolute paths. E.g. RUNTIME=bin, LIBRARY=lib →
                # RELATIVE_PATH("/bin", "/lib") = "../lib".
                file(RELATIVE_PATH _rpath_suffix
                    "/${ARG_RUNTIME_DESTINATION}"
                    "/${ARG_LIBRARY_DESTINATION}"
                )
                if(APPLE)
                    set(_rpath_prefix "@loader_path")
                else()
                    set(_rpath_prefix "$ORIGIN")
                endif()
                if(_rpath_suffix)
                    set(_rpath "${_rpath_prefix}/${_rpath_suffix}")
                else()
                    set(_rpath "${_rpath_prefix}")
                endif()
                set_target_properties("${target}" PROPERTIES INSTALL_RPATH "${_rpath}")
            endif()
        endif()
    endif()

    # If the SDK was built with Crashpad support, install the crashpad_handler
    # executable to RUNTIME_DESTINATION
    if((DD_CRASH_MODE STREQUAL "crashpad") OR (DATADOG_BUILT_WITH_DD_CRASH_MODE STREQUAL "crashpad"))
        if(TARGET crashpad::handler)
            get_target_property(CRASHPAD_HANDLER_PATH crashpad::handler IMPORTED_LOCATION)
            install(PROGRAMS ${CRASHPAD_HANDLER_PATH} DESTINATION "${ARG_RUNTIME_DESTINATION}")
        else()
            message(FATAL_ERROR "datadog_install(): target crashpad::handler does not exist")
        endif()
    endif()
endfunction()
