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

// To enable Crash Reporting support in your application, the SDK must be compiled with
// DD_CRASH_MODE=crashpad. At present, if you use Crashpad, your application build must
// target C++20, and you must distribute the crashpad_handler executable alongside your
// application.

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
 * Sets the path to the crash handler executable, which must be distributed alongside
 * your application: the SDK's Crashpad client will attempt to launch this process from
 * the provided path.
 *
 * By default, the SDK will look for a file named 'crashpad_handler' (POSIX) or
 * 'crashpad_handler.exe' (Windows) in the same directory as your application's
 * executable. If you distribute the handler executable in a different location, and/or
 * with a different name, provide the full file path to this function.
 *
 * If you provide a relative path, it will be resolved relative to the current working
 * directory.
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
