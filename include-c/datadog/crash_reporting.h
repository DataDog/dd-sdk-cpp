// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_CRASH_REPORTING_H
#define DATADOG_INCLUDE_CRASH_REPORTING_H

#include <inttypes.h>

#include "datadog/api.h"
#include "datadog/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// These values establish the size of string buffers in the C API
#define DATADOG_MAX_CRASH_REPORTING_HANDLER_EXE_PATH_LEN 1023

// === Crash Reporting configuration ===

// The exact crash reporting mechanism varies depending on which crash handler
// implementation the SDK is built with, controllable via the CMake option
// DD_CRASH_MODE:
//
// - DD_CRASH_MODE=noop
//   - Renders Crash Reporting inert: API remains usable but all calls are no-ops.
//
// - DD_CRASH_MODE=inprocess
//   - This is the default option, and the option used for pre-built SDK binaries.
//   - Captures the callstack for the crashing thread only.
//   - Uploads reports to Datadog on the next application launch.
//
// - DD_CRASH_MODE=crashpad
//   - This option is NOT YET SUPPORTED.
//   - Includes Crashpad in the SDK build; requires targeting C++20 and distributing a
//     datadog_crashpad_handler with your application; does not yet upload reports to
//     Datadog.

/**
 * Crash reporting configuration struct: initialize with
 * dd_crash_reporting_config_init(), then call dd_crash_reporting_config_set_<value>().
 */
typedef struct dd_crash_reporting_config {
  uint32_t version;
  char handler_exe_path[DATADOG_MAX_CRASH_REPORTING_HANDLER_EXE_PATH_LEN + 1];
} dd_crash_reporting_config_t;

/**
 * Initializes a dd_crash_reporting_config_t struct with default values.
 */
DATADOG_API void dd_crash_reporting_config_init(dd_crash_reporting_config_t* config);

/**
 * Sets the path to the crash handler executable, overriding the default path.
 *
 * If using DD_CRASH_MODE=crashpad (not yet supported), you may supply the full path to
 * the datadog_crashpad_handler executable to be launched alongside your application. By
 * default, the SDK will look for a file named 'datadog_crashpad_handler' (POSIX) or
 * 'datadog_crashpad_handler.exe' (Windows) in the same directory as your application's
 * executable.
 *
 * If using DD_CRASH_MODE=noop or DD_CRASH_MODE=inprocess, this value is ignored.
 */
DATADOG_API void dd_crash_reporting_config_set_handler_exe_path(
    dd_crash_reporting_config_t* config, const char* value
);

// === Crash Reporting feature interface ===

// You must register Crash Reporting as a feature after calling dd_core_create() and
// before calling dd_core_start().

/**
 * Interface to the Datadog SDK's Crash Reporting feature. Use dd_crash_reporting_init()
 * to register the feature with the core. You MUST call dd_crash_reporting_destroy()
 * when done.
 */
typedef struct dd_crash_reporting dd_crash_reporting_t;

/**
 * Registers the Crash Reporting feature with the core of the Datadog SDK. MUST be
 * matched with a call to dd_crash_reporting_destroy().
 */
DATADOG_API dd_crash_reporting_t* dd_crash_reporting_init(
    dd_core_t* core, const dd_crash_reporting_config_t* config
);

/**
 * Frees all memory allocated for the Crash Reporting feature.
 */
DATADOG_API void dd_crash_reporting_destroy(dd_crash_reporting_t* crash_reporting);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_CRASH_REPORTING_H
