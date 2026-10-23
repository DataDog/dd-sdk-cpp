# Given a shared-library target, adds install rules to ensure that a separate debug-info
# file (.pdb on Windows; .dylib.dSYM on macOS; .so.debug on Linux) will be installed
# alongside the binary (.dll, .dylib, or .so).
#
# In configurations that don't include debug info (e.g. Release and MinSizeRel), these
# rules do nothing. In Debug and RelWithDebInfo, `cmake --install` (or building the
# `package` target) will copy the linker-generated PDB on Windows, run dsymutil to
# generate a .dSYM on macOS, and run objcopy on Linux to split debug info out of the
# installed .so.
#
# This work is deliberately done at install time only: it primarily serves to shape the
# artifacts that we distribute in our release process. In development builds of the SDK,
# and in projects that consume the SDK via FetchContent (where DD_BUILD_INSTALL is off
# by default), the library in the build tree is left exactly as the build produced it.
function(install_shared_library_debug_info target_name library_destination runtime_destination)
    if(MSVC)
        # When linking with /DEBUG (as CMake does for Debug and RelWithDebInfo), the
        # linker writes a self-contained PDB for the DLL; OPTIONAL skips this rule for
        # configurations that don't produce one
        install(FILES "$<TARGET_PDB_FILE:${target_name}>"
            DESTINATION ${runtime_destination}
            OPTIONAL
        )
    elseif(APPLE)
        # On macOS, the linker doesn't copy DWARF into the dylib: it records a "debug
        # map" of paths to the object files that contain it, which are only valid on the
        # build machine. dsymutil follows that map to produce a standalone .dSYM bundle,
        # which tools match to the dylib by UUID (one per architecture, for universal
        # builds).
        find_program(DD_DSYMUTIL_EXECUTABLE dsymutil REQUIRED)
        set(install_code [[
            if($<CONFIG:Debug,RelWithDebInfo>)
                set(dsym "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/@library_destination@/@library_file_name@.dSYM")
                message(STATUS "Installing: ${dsym}")
                file(REMOVE_RECURSE "${dsym}")
                execute_process(
                    COMMAND "@DD_DSYMUTIL_EXECUTABLE@" "@library_file@" -o "${dsym}"
                    COMMAND_ERROR_IS_FATAL ANY
                )
            endif()
        ]])
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        if(NOT CMAKE_OBJCOPY)
            message(FATAL_ERROR "objcopy is required to split debug info from ${target_name}")
        endif()

        # Copy debug sections from the library in the build tree (which always has
        # them) to a .debug file, then strip debug sections from the installed library
        # and point its .gnu_debuglink at that file. The debug link is removed and
        # re-added in two steps so that re-running the install (when the installed
        # library is already up to date, and therefore already stripped) is harmless.
        set(install_code [[
            if($<CONFIG:Debug,RelWithDebInfo>)
                set(library "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/@library_destination@/@library_file_name@")
                message(STATUS "Installing: ${library}.debug")
                execute_process(
                    COMMAND "@CMAKE_OBJCOPY@" --only-keep-debug "@library_file@" "${library}.debug"
                    COMMAND_ERROR_IS_FATAL ANY
                )
                execute_process(
                    COMMAND "@CMAKE_OBJCOPY@" --strip-debug --remove-section=.gnu_debuglink "${library}"
                    COMMAND_ERROR_IS_FATAL ANY
                )
                execute_process(
                    COMMAND "@CMAKE_OBJCOPY@" "--add-gnu-debuglink=${library}.debug" "${library}"
                    COMMAND_ERROR_IS_FATAL ANY
                )
            endif()
        ]])
    else()
        return()
    endif()

    # Substitute configure-time values (@VAR@) into the install script, leaving ${VAR}
    # references to be resolved at install time; generator expressions in the resulting
    # code are evaluated by install(CODE)
    if(install_code)
        set(library_file "$<TARGET_FILE:${target_name}>")
        set(library_file_name "$<TARGET_FILE_NAME:${target_name}>")
        string(CONFIGURE "${install_code}" install_code @ONLY)
        install(CODE "${install_code}")
    endif()
endfunction()
