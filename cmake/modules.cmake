# The SDK is built as a set of modules: `types`, `core`, and one module per feature. Each
# module has its own source root under src/<module>/, and that root mirrors the include
# paths its code uses (datadog/c/, datadog/cpp/, and datadog/impl/<module>/). A module's
# root is on the include path only for the module itself and for modules that depend on
# it, so a module can only include another module's internal headers by declaring that
# dependency.
#
# Each module's target is an object library named `ddsdkcpp_<module>`. Static and shared
# builds alike link every module's objects into a single library, `ddsdkcpp` (aliased as
# `Datadog::sdk`): see src/CMakeLists.txt. Modules call each other's internal functions,
# so separate shared libraries would have to export those functions alongside the public
# API; a single library exports only the public API.
#
# In static builds, the CMake package also exports each module's object library. It
# installs no files, but it carries the module's link dependencies (e.g. libcurl in core)
# to consumers of the static `ddsdkcpp`, which records each module it's built from as a
# link dependency.

# dd_add_module(<name>
#     [DEPENDS <module>...]
#     [PUBLIC_HEADERS <header>...]
#     SOURCES <file>...
# )
#
# Defines the target for module <name>, built from SOURCES (relative to the calling
# CMakeLists.txt), and records it in the DD_MODULES global property, in the order in
# which modules are added.
#
# DEPENDS names the modules that this module's code includes and calls. Each must have
# been added already.
#
# PUBLIC_HEADERS lists the header names (e.g. `rum`) that the module implements, from the
# public API in include-c/ (as <name>.h) and include-cpp/ (as <name>.hpp). Only the
# public headers of modules in the build are installed and included in the umbrella
# headers (datadog.h and datadog.hpp).
function(dd_add_module name)
    cmake_parse_arguments(ARG "" "" "DEPENDS;PUBLIC_HEADERS;SOURCES" ${ARGN})
    set(target ddsdkcpp_${name})

    foreach(dependency IN LISTS ARG_DEPENDS)
        if(NOT TARGET ddsdkcpp_${dependency})
            message(FATAL_ERROR "The '${name}' module depends on the '${dependency}' module, which has not been added")
        endif()
    endforeach()

    add_library(${target} OBJECT ${ARG_SOURCES})
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)

    # Dependencies are PUBLIC, since a module's headers include its dependencies' headers
    foreach(dependency IN LISTS ARG_DEPENDS)
        target_link_libraries(${target} PUBLIC ddsdkcpp_${dependency})
    endforeach()

    # Expose this module's source root to itself and to its dependents within the build,
    # but not to installed consumers, which only see the public headers
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    )

    dd_configure_sdk_target(${target})

    set_property(TARGET ${target} PROPERTY DD_PUBLIC_HEADERS ${ARG_PUBLIC_HEADERS})
    set_property(GLOBAL APPEND PROPERTY DD_MODULES ${name})

    if(DD_BUILD_INSTALL)
        # Export this module's link dependencies to consumers of the static library (see
        # above). A shared library resolves them itself.
        if(NOT DD_BUILD_SHARED)
            install(TARGETS ${target} EXPORT DatadogTargets)
        endif()
        foreach(header IN LISTS ARG_PUBLIC_HEADERS)
            install(FILES
                ${DD_SDK_ROOT_DIR}/include-c/datadog/${header}.h
                ${DD_SDK_ROOT_DIR}/include-cpp/datadog/${header}.hpp
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/datadog
            )
        endforeach()
    endif()
endfunction()

# dd_configure_sdk_target(<target>)
#
# Applies the compile and link settings shared by every module's target and by the
# library built from them.
function(dd_configure_sdk_target target)
    # Building the SDK requires either C++17 or C++20 at a minimum, depending on
    # configuration. (The C++ API adheres to C++17, and the C API adheres to C99.)
    if(DD_FEATURE_CRASH_REPORTING AND DD_CRASH_MODE STREQUAL "crashpad")
        target_compile_features(${target} PUBLIC cxx_std_20)
    else()
        target_compile_features(${target} PUBLIC cxx_std_17)
    endif()

    # Add internal-only, header-only dependencies as PRIVATE SYSTEM includes
    target_include_external_deps(${target})

    target_enable_strict_warnings(${target})
    target_enable_parallel_compilation(${target})
    target_enable_datadog_assert(${target})
    target_enable_shared_library_support(${target})
    target_enable_clang_tidy(${target})
    target_enable_coverage(${target})
    target_enable_sanitizers(${target})

    # The in-process crash handler's POSIX stack-walking depends on intact frame
    # pointers, and SDK code from any module may be on the stack when a crash occurs
    if(DD_FEATURE_CRASH_REPORTING AND DD_CRASH_MODE STREQUAL "inprocess" AND UNIX)
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer)
    endif()
endfunction()

# dd_get_module_targets(<out_var>)
#
# Sets <out_var> to the targets of every module in the build, in the order in which they
# were added (i.e. dependencies first).
function(dd_get_module_targets out_var)
    get_property(modules GLOBAL PROPERTY DD_MODULES)
    list(TRANSFORM modules PREPEND "ddsdkcpp_")
    set(${out_var} ${modules} PARENT_SCOPE)
endfunction()
