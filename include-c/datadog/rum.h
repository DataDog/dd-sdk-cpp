// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_RUM_H
#define DATADOG_INCLUDE_RUM_H

#include <stddef.h>
#include <stdint.h>

#include "datadog/api.h"
#include "datadog/attribute.h"
#include "datadog/core.h"

#ifdef __cplusplus
extern "C" {
#endif

// === RUM configuration ===

/**
 * RUM configuration struct: initialize with dd_rum_config_init(), then call
 * dd_rum_config_set_<value>().
 */
typedef struct dd_rum_config {
  uint32_t version;
  const char* application_id;
} dd_rum_config_t;

/**
 * Initializes a dd_rum_config_t with all required settings.
 *
 * @param application_id The ID of your RUM Application.
 */
DATADOG_API void dd_rum_config_init(
    dd_rum_config_t* config, const char* application_id
);

/**
 * Sets the RUM Application ID, overriding the value passed to dd_rum_config_init().
 */
DATADOG_API void dd_rum_config_set_application_id(
    dd_rum_config_t* config, const char* value
);

// === RUM feature interface ===

/**
 * Interface to the Datadog SDK's RUM feature. Use dd_rum_init() to register the
 * RUM feature with the core. You MUST call dd_rum_destroy() when done.
 */
typedef struct dd_rum dd_rum_t;

/**
 * Registers the RUM feature with the core of the Datadog SDK. MUST be matched with
 * a call to dd_rum_destroy().
 */
DATADOG_API dd_rum_t* dd_rum_init(dd_core_t* core, const dd_rum_config_t* config);

/**
 * Frees all memory allocated for the RUM feature.
 */
DATADOG_API void dd_rum_destroy(dd_rum_t* rum);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_RUM_H
