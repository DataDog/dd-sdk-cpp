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
//     crashpad_handler with your application; does not yet upload reports to Datadog.

/**
 * Crash Reporting configuration. Create a config with
 * dd_crash_reporting_config_create(), set optional values with
 * dd_crash_reporting_config_set_<value>(), pass it to dd_crash_reporting_init(), and
 * then release it with dd_crash_reporting_config_destroy().
 *
 * Setters copy any string arguments, so the caller needs to keep those arguments valid
 * only for the duration of the call. Setters have no effect when given a NULL config.
 */
typedef struct dd_crash_reporting_config dd_crash_reporting_config_t;

/**
 * Creates a new Crash Reporting config with default settings. You MUST call
 * dd_crash_reporting_config_destroy() when finished with the config.
 */
DATADOG_API dd_crash_reporting_config_t* dd_crash_reporting_config_create(void);

/**
 * Frees all memory allocated for the given config. A Crash Reporting feature
 * initialized from this config is unaffected. Has no effect if `config` is NULL.
 */
DATADOG_API void dd_crash_reporting_config_destroy(dd_crash_reporting_config_t* config);

/**
 * Sets the path to the crash handler executable, overriding the default path.
 *
 * If using DD_CRASH_MODE=crashpad (not yet supported), you may supply the full path to
 * the crashpad_handler executable to be launched alongside your application. By
 * default, the SDK will look for a file named 'crashpad_handler' (POSIX) or
 * 'crashpad_handler.exe' (Windows) in the same directory as your application's
 * executable.
 *
 * If using DD_CRASH_MODE=noop or DD_CRASH_MODE=inprocess, this value is ignored. Both
 * NULL and "" restore the default path.
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
 *
 * If `config` is NULL, the default configuration values will be used. Otherwise, the
 * feature keeps its own copy of the configuration, so the caller may destroy `config`
 * as soon as this call returns.
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
