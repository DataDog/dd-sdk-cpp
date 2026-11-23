# crash_reporting: the Crash Reporting feature, which installs a crash handler (selected
# by DD_CRASH_MODE) and processes the crash reports it writes.
dd_add_module(crash_reporting
    DEPENDS core
    PUBLIC_HEADERS crash_reporting
    SOURCES
        datadog/c/crash_reporting_c_api.cpp
        datadog/cpp/crash_reporting_cpp_api.cpp
        datadog/impl/crash_reporting/data/crash_context_read.cpp
        datadog/impl/crash_reporting/data/crash_report_read.cpp
        datadog/impl/crash_reporting/crash_handler_init.cpp
        datadog/impl/crash_reporting/crash_processing.cpp
        datadog/impl/crash_reporting/crash_reporting.cpp
)

if(DD_CRASH_MODE STREQUAL "noop")
    # No-op crash handler implementation requires no additional code
    target_sources(ddsdkcpp_crash_reporting PRIVATE
        datadog/impl/crash_reporting/handlers/noop/crash_handler_noop.cpp
    )
elseif(DD_CRASH_MODE STREQUAL "inprocess")
    # In-process crash handler needs to write binary crash report files
    target_sources(ddsdkcpp_crash_reporting PRIVATE
        datadog/impl/crash_reporting/data/crash_context_write.cpp
        datadog/impl/crash_reporting/data/crash_report_write.cpp
    )

    # In-process crash handler needs to cache module build IDs, in addition to compiling
    # a platform-specific ICrashHandler implementation (POSIX or Windows). On POSIX, its
    # stack-walking depends on intact frame pointers, so every module is compiled with
    # -fno-omit-frame-pointer in this configuration (see cmake/modules.cmake).
    target_sources(ddsdkcpp_crash_reporting PRIVATE
        datadog/impl/crash_reporting/handlers/inprocess/buildid_cache.cpp
    )
    if(WIN32)
        target_sources(ddsdkcpp_crash_reporting PRIVATE
            datadog/impl/crash_reporting/handlers/inprocess/crash_handler_inprocess_windows.cpp
        )
    elseif(UNIX)
        target_sources(ddsdkcpp_crash_reporting PRIVATE
            datadog/impl/crash_reporting/handlers/inprocess/crash_handler_inprocess_posix.cpp
        )
    else()
        message(FATAL_ERROR "In-process crash handler not supported on this platform")
    endif()
elseif(DD_CRASH_MODE STREQUAL "crashpad")
    # If building with crashpad support, integrate crashpad as a dependency: this
    # adds an ExternalProject target that will build crashpad, then adds the
    # resulting static lib as a dependency for this module. To ensure binary-compatible
    # builds, this script must run after compile/link options for this module are fully
    # configured.
    include(${DD_SDK_ROOT_DIR}/cmake/crashpad.cmake)
    target_sources(ddsdkcpp_crash_reporting PRIVATE
        datadog/impl/crash_reporting/handlers/crashpad/crash_handler_crashpad.cpp
        datadog/impl/crash_reporting/handlers/crashpad/view_event_fit.cpp
    )
else()
    message(FATAL_ERROR "Invalid DD_CRASH_MODE: ${DD_CRASH_MODE}. Must be 'noop', 'inprocess', or 'crashpad'")
endif()
