# Builds the SDK from its modules (see cmake/modules.cmake). Each module's subdirectory
# defines its target, including any choice between implementations within that module
# (e.g. DD_HTTP_CLIENT in core, DD_CRASH_MODE in crash_reporting). This file only
# selects which modules to build, in dependency order, and defines the targets and
# headers that span all of them.

# Umbrella headers are generated here, then exposed to the build with the public headers
set(DD_GENERATED_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/include)

# Include paths and definitions for compiling against the public API. Both the types
# module and `ddsdkcpp` declare them as PUBLIC: every other module links types (directly
# or through core), so every module compiles with them, and code that links `ddsdkcpp`
# gets them from it. `ddsdkcpp` links its modules PRIVATE, so it doesn't pass on what it
# gets from types.
set(DD_SDK_PUBLIC_INCLUDE_DIRS
    $<BUILD_INTERFACE:${DD_SDK_ROOT_DIR}/include-c>
    $<BUILD_INTERFACE:${DD_SDK_ROOT_DIR}/include-cpp>
    $<BUILD_INTERFACE:${DD_GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>
)
# Inject version number from CMake project config
set(DD_SDK_PUBLIC_DEFINITIONS DATADOG_BUILD_VERSION="${ddsdkcpp_VERSION}")
if(DD_GIT_REVISION_ID)
    list(APPEND DD_SDK_PUBLIC_DEFINITIONS DATADOG_GIT_REVISION_ID="${DD_GIT_REVISION_ID}")
endif()
# When building a universal (multi-architecture) binary, the preprocessor arch
# detection in version.hpp would resolve to whichever slice is being compiled.
# Signal the universal case explicitly so the artifact name reflects it.
list(LENGTH CMAKE_OSX_ARCHITECTURES _dd_arch_count)
if(_dd_arch_count GREATER 1)
    list(APPEND DD_SDK_PUBLIC_DEFINITIONS DATADOG_BUILD_IS_UNIVERSAL)
endif()

add_subdirectory(types)
if(DD_MODULE_CORE)
    add_subdirectory(core)
endif()
if(DD_MODULE_CRASH_PROCESSING)
    add_subdirectory(crash_processing)
endif()
if(DD_MODULE_LOGGING)
    add_subdirectory(logging)
endif()
if(DD_MODULE_RUM)
    add_subdirectory(rum)
endif()
if(DD_MODULE_CRASH_REPORTING)
    add_subdirectory(crash_reporting)
endif()
if(DD_MODULE_PROFILING)
    add_subdirectory(profiling)
endif()

# `ddsdkcpp` (Datadog::sdk) is the single library, static or shared, that contains every
# module in the build. Linking each module's object library adds its objects to the
# library, along with its link dependencies.
if(DD_BUILD_SHARED)
    set(DD_LIBRARY_TYPE SHARED)
else()
    set(DD_LIBRARY_TYPE STATIC)
endif()
dd_get_module_targets(DD_MODULE_TARGETS)
add_library(ddsdkcpp ${DD_LIBRARY_TYPE})
target_link_libraries(ddsdkcpp PRIVATE ${DD_MODULE_TARGETS})
set_target_properties(ddsdkcpp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME ddsdkcpp
    EXPORT_NAME sdk
)
target_include_directories(ddsdkcpp PUBLIC ${DD_SDK_PUBLIC_INCLUDE_DIRS})
target_compile_definitions(ddsdkcpp PUBLIC ${DD_SDK_PUBLIC_DEFINITIONS})
dd_configure_sdk_target(ddsdkcpp)

# Generate datadog.h and datadog.hpp to include the public headers of every module in
# the build, so that the umbrella headers never refer to headers that aren't installed
set(DD_PUBLIC_HEADERS "")
foreach(module_target IN LISTS DD_MODULE_TARGETS)
    get_property(headers TARGET ${module_target} PROPERTY DD_PUBLIC_HEADERS)
    list(APPEND DD_PUBLIC_HEADERS ${headers})
endforeach()
list(SORT DD_PUBLIC_HEADERS)
set(DD_UMBRELLA_C_INCLUDES "")
set(DD_UMBRELLA_CPP_INCLUDES "")
foreach(header IN LISTS DD_PUBLIC_HEADERS)
    string(APPEND DD_UMBRELLA_C_INCLUDES "#include \"datadog/${header}.h\"\n")
    string(APPEND DD_UMBRELLA_CPP_INCLUDES "#include \"datadog/${header}.hpp\"\n")
endforeach()
configure_file(${DD_SDK_ROOT_DIR}/cmake/templates/datadog.h.in
    ${DD_GENERATED_INCLUDE_DIR}/datadog.h @ONLY)
configure_file(${DD_SDK_ROOT_DIR}/cmake/templates/datadog.hpp.in
    ${DD_GENERATED_INCLUDE_DIR}/datadog.hpp @ONLY)

# Install configuration
if(DD_BUILD_INSTALL)
    install(TARGETS ddsdkcpp
        EXPORT DatadogTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
    )
    install(FILES
        ${DD_GENERATED_INCLUDE_DIR}/datadog.h
        ${DD_GENERATED_INCLUDE_DIR}/datadog.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    # Install debug info for shared-library builds as separate files alongside the
    # library: see debug-info.cmake for details
    if(DD_BUILD_SHARED)
        include(${DD_SDK_ROOT_DIR}/cmake/debug-info.cmake)
        install_shared_library_debug_info(ddsdkcpp lib bin)
    endif()
endif()
