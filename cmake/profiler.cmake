include(FetchContent)

# Allow overriding the GitHub fetch with a local clone of dd-win-prof. When
# set, CMake skips the network fetch and uses the local source directory
# directly.
set(DD_WIN_PROF_SOURCE_DIR "" CACHE PATH
    "Local path to dd-win-prof source tree (overrides GitHub fetch)")

if(DD_WIN_PROF_SOURCE_DIR)
    FetchContent_Declare(
        dd-win-prof
        SOURCE_DIR "${DD_WIN_PROF_SOURCE_DIR}"
    )
else()
    # TODO: Pin to a released tag once the split RUM context API is merged.
    # For now, this points to a specific commit on main.
    FetchContent_Declare(
        dd-win-prof
        GIT_REPOSITORY https://github.com/DataDog/dd-win-prof.git
        GIT_TAG        13dd010034bef0eda30179f7bafd9a4e6899aa41
    )
endif()

FetchContent_MakeAvailable(dd-win-prof)

# Link dd-win-prof into the SDK as a public dependency so that consumers can
# include dd-win-prof.h directly if needed.
target_link_libraries(dd_native PUBLIC dd-win-prof)

if(DD_BUILD_INSTALL)
    # Install dd-win-prof.dll to bin/ so it ships alongside the SDK. Include it in
    # the DatadogTargets export set since dd_native depends on it.
    install(TARGETS dd-win-prof
        EXPORT DatadogTargets
        RUNTIME DESTINATION bin
    )

    # Install datadog_profiling_ffi.dll, which dd-win-prof.dll loads at runtime.
    install(FILES "$<TARGET_FILE:libdatadog_dynamic>" DESTINATION bin)
endif()
