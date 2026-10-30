# Builds dd-win-prof (the Windows profiler DLL) as a dependency and links it into
# ddsdkcpp. Included from src/CMakeLists.txt, after ddsdkcpp is configured, only when
# DD_PROFILER_IMPL is "dd-win-prof" (i.e. DD_ENABLE_PROFILING on Windows x64).
#
# dd-win-prof is linked normally rather than delay-loaded: it discovers threads through
# DllMain, so it has to be loaded at process start to see the threads created before the
# SDK initializes. Applications that build with profiling therefore need
# dd-win-prof.dll alongside them (datadog_enable() / datadog_install() handle this).
#
# dd-win-prof.dll is the only runtime file needed: it links libdatadog's static library,
# so the FFI code is compiled into the DLL and it does not import
# datadog_profiling_ffi.dll (check with `dumpbin /dependents dd-win-prof.dll`).
# dd-win-prof's own README and dd_win_prof_copy_runtime_deps still mention that DLL, but
# only its own test executable loads it.

include(FetchContent)

set(DD_WIN_PROF_GIT_TAG "7caf25bfce0e55e9df3d60c9baf0b1dfa408f97e" CACHE STRING
    "dd-win-prof revision (commit SHA) to build")

# To work on dd-win-prof alongside the SDK, point this at a local checkout
set(DD_WIN_PROF_SOURCE_DIR "" CACHE PATH
    "Local dd-win-prof source tree to build instead of fetching DD_WIN_PROF_GIT_TAG")

if(DD_WIN_PROF_SOURCE_DIR)
    # FetchContent writes SOURCE_DIR into a generated file without escaping it, so
    # Windows backslashes must be converted first
    file(TO_CMAKE_PATH "${DD_WIN_PROF_SOURCE_DIR}" _dd_win_prof_source_dir)
    FetchContent_Declare(dd-win-prof SOURCE_DIR "${_dd_win_prof_source_dir}")
else()
    FetchContent_Declare(
        dd-win-prof
        GIT_REPOSITORY https://github.com/DataDog/dd-win-prof.git
        GIT_TAG "${DD_WIN_PROF_GIT_TAG}"
    )
endif()
FetchContent_MakeAvailable(dd-win-prof)

# Only dd-win-prof.h is exposed by the dd-win-prof target, and it stays private to the
# SDK: it is not part of the SDK's public API
target_link_libraries(ddsdkcpp PRIVATE dd-win-prof)
target_compile_definitions(ddsdkcpp PRIVATE WITH_DATADOG_PROFILING=1)
target_sources(ddsdkcpp PRIVATE
    ${DD_SDK_ROOT_DIR}/src/datadog/impl/profiling/dd_win_prof_profiler_backend.cpp
)

# dd-win-prof's own install rules are disabled when it is built as a dependency. The DLL
# is needed next to the application at runtime, and (for a static ddsdkcpp) its import
# library is needed to link, so install and export it with the SDK.
if(DD_BUILD_INSTALL)
    install(TARGETS dd-win-prof
        EXPORT DatadogTargets
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
    )
endif()
