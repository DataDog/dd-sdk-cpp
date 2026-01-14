# Crashpad is part of the Chromium project, and it uses a Google-specific toolchain
# (depot_tools, gclient, gn, ninja) rather than supporting CMake. We have a script in
# tools/bootstrap-crashpad/ that streamlines the process of cloning and building
# crashpad with its natively-supported tools, and we use the ExternalProject module to
# integrate that build process into our CMake build.
include(ExternalProject)

# Our main library target for the C++ SDK (dd_native) will need to link against the
# crashpad_client static lib once it's built. To ensure binary compatibility, we need to
# resolve the relevant details of our CMake build configuration, so that we can produce
# a GN build with the compatible set of options.
get_target_property(DD_COMPILE_OPTIONS dd_native COMPILE_OPTIONS)
get_target_property(DD_LINK_OPTIONS dd_native LINK_OPTIONS)

# Convert lists to pipe-separated strings to avoid shell parsing issues
# (CMake lists use semicolons, which would be expanded to separate arguments)
# We use pipe '|' as delimiter since it won't appear in compiler flags
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

# Declare an ExternalProject target, which we can build in order to bootstrap the
# crashpad repo with all required tools and then build the static library. Note that
# crashpad_external only encapsulates the commands required to download and build
# crashpad; it doesn't tell CMake anything about that build's artifacts or dependencies
# TODO(RUM-12207): Better support for incremental builds
ExternalProject_Add(crashpad_external
    DOWNLOAD_COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/bootstrap-crashpad/main.py install
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tools/bootstrap-crashpad/main.py build
        --no-install
        -c CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD}
        -c CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -c CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -c DD_COMPILE_OPTIONS=${DD_COMPILE_OPTIONS_ENCODED}
        -c DD_LINK_OPTIONS=${DD_LINK_OPTIONS_ENCODED}
    INSTALL_COMMAND ""
    USES_TERMINAL_DOWNLOAD TRUE
    USES_TERMINAL_BUILD TRUE
    BUILD_ALWAYS TRUE
)

# Resolve the expected path to the static lib file for the crashpad client library
if(WIN32)
    set(CRASHPAD_CLIENT_LIB_PATH ${CMAKE_SOURCE_DIR}/chromium/crashpad/crashpad/out/Default/obj/client/client.lib)
else()
    set(CRASHPAD_CLIENT_LIB_PATH ${CMAKE_SOURCE_DIR}/chromium/crashpad/crashpad/out/Default/obj/client/libclient.a)
endif()

# Establish a CMake target to represent the crashpad client static library: IMPORTED
# creates a logical target that CMake expects to already be present at link-time rather
# than building the library itself
add_library(crashpad::client STATIC IMPORTED)
set_target_properties(crashpad::client PROPERTIES
    IMPORTED_LOCATION ${CRASHPAD_CLIENT_LIB_PATH}
)

# Establish that this static library file is produced by building crashpad_external
add_custom_command(
    OUTPUT ${CRASHPAD_CLIENT_LIB_PATH}
    DEPENDS crashpad_external
    COMMENT "Building crashpad via external project"
)

# Add a custom target to establish that depending on crashpad::client requires the
# static library file to be built as a prerequisite
add_custom_target(crashpad_client_file
    DEPENDS ${CRASHPAD_CLIENT_LIB_PATH}
)
add_dependencies(crashpad::client crashpad_client_file)

# Update the main library target for the SDK to link against the crashpad client
target_link_libraries(dd_native PRIVATE crashpad::client)
