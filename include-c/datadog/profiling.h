// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_PROFILING_H
#define DATADOG_INCLUDE_PROFILING_H

#include <inttypes.h>

#include "datadog/api.h"
#include "datadog/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// === Profiling configuration ===

// Profiling is only available when the SDK is built for Windows x64 with the CMake
// option DD_ENABLE_PROFILING=ON. In all other builds, the API remains usable but all
// calls are no-ops.

/**
 * Profiling configuration. It has no settings yet. Create a config with
 * dd_profiling_config_create(), pass it to dd_profiling_init(), and then release it
 * with dd_profiling_config_destroy().
 */
typedef struct dd_profiling_config dd_profiling_config_t;

/**
 * Creates a new Profiling config with default settings. You MUST call
 * dd_profiling_config_destroy() when finished with the config.
 */
DATADOG_API dd_profiling_config_t* dd_profiling_config_create(void);

/**
 * Frees all memory allocated for the given config. A Profiling feature initialized from
 * this config is unaffected. Has no effect if `config` is NULL.
 */
DATADOG_API void dd_profiling_config_destroy(dd_profiling_config_t* config);

// === Profiling feature interface ===

// You must register Profiling as a feature after calling dd_core_create() and before
// calling dd_core_start().
//
// When dd_core_start() is called, the feature starts profiling the application, and
// attributes the profiles to the service, environment, and version provided to the
// core. Each profile is correlated with the RUM events, such as vitals and long tasks,
// that occurred while it was collected. A profile with no such events to correlate it
// with is not kept.
//
// Profiling data may be gathered while the core is running and tracking consent is
// DD_TRACKING_CONSENT_GRANTED or DD_TRACKING_CONSENT_PENDING. The profiler does not run
// while tracking consent is DD_TRACKING_CONSENT_NOT_GRANTED.

/**
 * Interface to the Datadog SDK's Profiling feature. Use dd_profiling_init() to register
 * the feature with the core. You MUST call dd_profiling_destroy() when done.
 */
typedef struct dd_profiling dd_profiling_t;

/**
 * Registers the Profiling feature with the core of the Datadog SDK. MUST be matched
 * with a call to dd_profiling_destroy().
 *
 * If `config` is NULL, the default configuration values will be used. The caller may
 * destroy `config` as soon as this call returns.
 *
 * The profiler is shared by the whole process, so only one core can use it at a time.
 * If another core has already registered Profiling, or if this build has no profiler,
 * this logs a diagnostic message and returns NULL, which is safe to pass to all other
 * dd_profiling functions.
 */
DATADOG_API dd_profiling_t* dd_profiling_init(
    dd_core_t* core, const dd_profiling_config_t* config
);

/**
 * Frees all memory allocated for the Profiling feature handle. The feature itself stays
 * registered with the core until the core is destroyed.
 */
DATADOG_API void dd_profiling_destroy(dd_profiling_t* profiling);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_PROFILING_H
