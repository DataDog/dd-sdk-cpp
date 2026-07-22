# Crashpad is part of the Chromium project, and it uses a Google-specific toolchain
# (depot_tools, gclient, gn, ninja) rather than supporting CMake. We have a script in
# tools/bootstrap-crashpad/ that streamlines the process of cloning and building
# crashpad with its natively-supported tools, and we use the ExternalProject module to
# integrate that build process into our CMake build.
include(ExternalProject)

# We'll write Crashpad build files and artifacts to a dedicated directory within the
# current CMake build directory
set(CRASHPAD_BUILD_DIR ${CMAKE_BINARY_DIR}/_deps/crashpad-build)

# The `ddsdkcpp` target will need to link against the crashpad_client static lib once
# it's built. To ensure binary compatibility, we need to resolve the relevant details
# of our CMake build configuration, so that we can produce a GN build with the
# compatible set of options.
get_target_property(DD_COMPILE_OPTIONS ddsdkcpp COMPILE_OPTIONS)
get_target_property(DD_LINK_OPTIONS ddsdkcpp LINK_OPTIONS)

# Convert lists to pipe-separated strings to avoid shell parsing issues (CMake lists use
# semicolons, which would be expanded to separate arguments). We use pipe '|' as a
# delimiter since it won't appear in compiler flags.
if(DD_COMPILE_OPTIONS)
    string(REPLACE ";" "|" DD_COMPILE_OPTIONS_ENCODED "${DD_COMPILE_OPTIONS}")
else()
    set(DD_COMPILE_OPTIONS_ENCODED "")
endif()
if(DD_LINK_OPTIONS)
    string(REPLACE ";" "|" DD_LINK_OPTIONS_ENCODED "${DD_LINK_OPTIONS}")
else()
    set(DD_LINK_OPTIONS_ENCODED "")
endif()

# Our bootstrap script requires Python3: any interpreter version will do, since we don't
# require any pip packages, and since the crashpad build itself will use the python
# interpreter bundled with depot_tools
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Under normal circumstances, just build Crashpad without running its test suite, but
# allow tests to be run (in CI etc.) by supplying DD_RUN_CRASHPAD_TESTS=ON
option(DD_RUN_CRASHPAD_TESTS "Run Crashpad tests during build" OFF)
if(DD_RUN_CRASHPAD_TESTS)
    set(CRASHPAD_NO_TEST_ARG "")
else()
    set(CRASHPAD_NO_TEST_ARG "--no-test")
endif()

# When set, build Crashpad from this local clone (set up via `dev init`) instead of
# fetching and building the pinned revision via depot_tools. Intended for SDK developers
# who are iterating on Crashpad changes; see tools/bootstrap-crashpad/main.py for details.
set(DD_CRASHPAD_LOCAL_CLONE "" CACHE PATH
    "Path to a local crashpad clone to build from instead of fetching via depot_tools")
if(DD_CRASHPAD_LOCAL_CLONE)
    # Local-clone mode: the clone is managed by the developer via `dev init`; nothing to
    # fetch. Build using `dev build`, which sources from the local clone directly.
    set(CRASHPAD_DOWNLOAD_COMMAND "")
    set(CRASHPAD_BUILD_SUBCOMMAND dev build ${DD_CRASHPAD_LOCAL_CLONE})
else()
    # Normal mode: fetch (or sync) the pinned crashpad revision via depot_tools, apply
    # datadog.patch, then build.
    set(CRASHPAD_DOWNLOAD_COMMAND ${Python3_EXECUTABLE} ${DD_SDK_ROOT_DIR}/tools/bootstrap-crashpad/main.py install)
    set(CRASHPAD_BUILD_SUBCOMMAND build --no-install)
endif()

# Declare an ExternalProject target, which we can build in order to bootstrap the
# crashpad repo with all required tools and then build the static library. Note that
# crashpad_external only encapsulates the commands required to download and build
# crashpad; it doesn't tell CMake anything about that build's artifacts or dependencies
ExternalProject_Add(crashpad_external
    DOWNLOAD_COMMAND ${CRASHPAD_DOWNLOAD_COMMAND}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${Python3_EXECUTABLE} -u ${DD_SDK_ROOT_DIR}/tools/bootstrap-crashpad/main.py ${CRASHPAD_BUILD_SUBCOMMAND}
        --out-dir ${CRASHPAD_BUILD_DIR}
        ${CRASHPAD_NO_TEST_ARG}
        -c CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
        -c CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -c CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -c MSVC_RUNTIME_LIBRARY=${MSVC_RUNTIME_LIBRARY}
        -c DD_COMPILE_OPTIONS=${DD_COMPILE_OPTIONS_ENCODED}
        -c DD_LINK_OPTIONS=${DD_LINK_OPTIONS_ENCODED}
    INSTALL_COMMAND ""
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_BUILD TRUE
    BUILD_ALWAYS TRUE
)

# Given the path to a file that's produced from the crashpad build, creates a custom
# target to represent that file and establishes a dependency so that CMake recognizes
# that the file can be produced by building the crashpad_external target
function(register_crashpad_artifact custom_target_name path)
    add_custom_command(OUTPUT ${path} DEPENDS crashpad_external)
    add_custom_target(${custom_target_name} DEPENDS ${path})
endfunction()

# Resolve the expected path to crashpad build artifacts that we'll need to consume later
# in the build
set(CRASHPAD_ARTIFACTS_PATH ${CRASHPAD_BUILD_DIR}/obj)
function(set_crashpad_lib_path OUT_VAR relpath name)
  if(WIN32)
    set(${OUT_VAR} "${CRASHPAD_ARTIFACTS_PATH}/${relpath}/${name}.lib" PARENT_SCOPE)
  else()
    set(${OUT_VAR} "${CRASHPAD_ARTIFACTS_PATH}/${relpath}/lib${name}.a" PARENT_SCOPE)
  endif()
endfunction()
function(set_crashpad_exe_path OUT_VAR name)
  if(WIN32)
    set(${OUT_VAR} "${CRASHPAD_BUILD_DIR}/${name}.exe" PARENT_SCOPE)
  else()
    set(${OUT_VAR} "${CRASHPAD_BUILD_DIR}/${name}" PARENT_SCOPE)
  endif()
endfunction()

# The 'crashpad_handler' binary must ultimately be deployed alongside final executable
# for any application that integrates the Datadog SDK with Crashpad support: this is a
# separate executable, launched via the Crashpad client, that receives IPC notifications
# from the client when a crash is detected, and that handles processing and uploading
# crash reports
set_crashpad_exe_path(CRASHPAD_HANDLER_EXE_PATH crashpad_handler)
register_crashpad_artifact(crashpad_handler_exe_file ${CRASHPAD_HANDLER_EXE_PATH})

# 'client' is the static lib that we'll link the SDK against in order to get
# crashpad::CrashpadClient et al. into our build. CRASHPAD_CLIENT_LINK_LIBRARIES will
# represent all of the additional linker dependencies required by the client.
set_crashpad_lib_path(CRASHPAD_CLIENT_LIB_PATH client client)
register_crashpad_artifact(crashpad_client_lib_file ${CRASHPAD_CLIENT_LIB_PATH})
set(CRASHPAD_CLIENT_LINK_LIBRARIES "")

# 'common' contains shared client functionality including CrashReportDatabase, Settings,
# and Annotation implementations that are used by both the client and handler
set_crashpad_lib_path(CRASHPAD_COMMON_LIB_PATH client common)
register_crashpad_artifact(crashpad_common_lib_file ${CRASHPAD_COMMON_LIB_PATH})
list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES ${CRASHPAD_COMMON_LIB_PATH})

# 'util' is a foundational support library required by client: it provides low-level,
# platform-specific utilities used by client et al. On macOS, util depends on
# 'mig_output', which contains code generated by MIG (Mach Interface Generator).
set_crashpad_lib_path(CRASHPAD_UTIL_LIB_PATH util util)
register_crashpad_artifact(crashpad_util_lib_file ${CRASHPAD_UTIL_LIB_PATH})
list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES ${CRASHPAD_UTIL_LIB_PATH})
if(APPLE)
    set_crashpad_lib_path(CRASHPAD_MIG_OUTPUT_LIB_PATH util mig_output)
    register_crashpad_artifact(crashpad_mig_output_lib_file ${CRASHPAD_MIG_OUTPUT_LIB_PATH})
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES ${CRASHPAD_MIG_OUTPUT_LIB_PATH})
endif()

# 'base' (from mini_chromium) provides platform abstractions and low-level utilities and
# is required by the crashpad client
set_crashpad_lib_path(CRASHPAD_BASE_LIB_PATH third_party/mini_chromium/mini_chromium/base base)
register_crashpad_artifact(crashpad_base_lib_file ${CRASHPAD_BASE_LIB_PATH})
list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES ${CRASHPAD_BASE_LIB_PATH})

# On macOS, Crashpad requires additional system frameworks
if(APPLE)
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES "-framework CoreFoundation")
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES "-framework CoreGraphics")
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES "-framework CoreText")
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES "-framework IOKit")
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES "-framework Foundation")
    list(APPEND CRASHPAD_CLIENT_LINK_LIBRARIES "-lbsm")
endif()

# Create an IMPORTED CMake target for the client library: depending on crashpad::client
# via target_link_libraries will ensure that the target links against libclient.a and
# all its linker dependencies
add_library(crashpad::client STATIC IMPORTED GLOBAL)
set_target_properties(crashpad::client PROPERTIES
    IMPORTED_LOCATION ${CRASHPAD_CLIENT_LIB_PATH}
    INTERFACE_LINK_LIBRARIES "${CRASHPAD_CLIENT_LINK_LIBRARIES}"
)
add_dependencies(crashpad::client crashpad_client_lib_file)

# Create an IMPORTED CMake target for the handler executable: we'll use this later in
# order to ensure that the handler is bundled with the application executable
add_executable(crashpad::handler IMPORTED GLOBAL)
set_target_properties(crashpad::handler PROPERTIES
    IMPORTED_LOCATION ${CRASHPAD_HANDLER_EXE_PATH}
)
add_dependencies(crashpad::handler crashpad_handler_exe_file)

# Update the `ddsdkcpp` target to link against the crashpad client
if(DD_CRASHPAD_LOCAL_CLONE)
    set(CRASHPAD_SOURCE_ROOT ${DD_CRASHPAD_LOCAL_CLONE})
else()
    set(CRASHPAD_SOURCE_ROOT ${DD_SDK_ROOT_DIR}/chromium/crashpad/crashpad)
endif()
target_include_directories(ddsdkcpp SYSTEM PRIVATE
    ${CRASHPAD_SOURCE_ROOT}
    ${CRASHPAD_SOURCE_ROOT}/third_party/mini_chromium/mini_chromium
)
target_link_libraries(ddsdkcpp PRIVATE crashpad::client)

# Establish a direct dependency between `ddsdkcpp` and `crashpad_external`: this
# ensures that CMake will download and build the crashpad source before attempting to
# compile the `ddsdkcpp` target
add_dependencies(ddsdkcpp crashpad_external)

# When generating an installed build of the SDK, include headers, artifacts, and CMake
# scripts for Crashpad as well
if(DD_BUILD_INSTALL)
    # Crashpad headers don't need to be distributed in include/, as they're only used
    # internally within the implementation layer of the SDK

    # Crashpad static libs should be distributed in lib/, as an application that links
    # against Datadog::sdk will be linked against crashpad::client transitively
    install(FILES ${CRASHPAD_CLIENT_LIB_PATH} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${CRASHPAD_COMMON_LIB_PATH} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${CRASHPAD_UTIL_LIB_PATH} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES ${CRASHPAD_BASE_LIB_PATH} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    if(APPLE)
        install(FILES ${CRASHPAD_MIG_OUTPUT_LIB_PATH} DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()

    # The Crashpad handler executable should be distributed in bin/
    install(PROGRAMS ${CRASHPAD_HANDLER_EXE_PATH} DESTINATION bin)

    # CrashpadConfig.cmake should be generated from the .cmake.in template and
    # distributed alongside DatadogConfig.cmake so that the relative include path
    # in DatadogConfig.cmake resolves correctly regardless of CMAKE_INSTALL_LIBDIR
    set(CRASHPAD_CONFIG_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/Crashpad)
    configure_file(
        ${CMAKE_SOURCE_DIR}/cmake/CrashpadConfig.cmake.in
        ${CMAKE_BINARY_DIR}/CrashpadConfig.cmake
        @ONLY
    )
    install(FILES ${CMAKE_BINARY_DIR}/CrashpadConfig.cmake
        DESTINATION ${CRASHPAD_CONFIG_INSTALL_DIR}
    )
endif()
