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
#include "datadog/uuid.h"

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
  dd_uuid_t application_id;
  float session_sample_rate;
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

/**
 * Sets the sample rate to a value between 0.0 and 100.0, indicating what percentage of
 * RUM sessions should be sampled. At 100.0, events for all sessions are sent to intake;
 * at 0.0, no RUM events are generated. Default is 100.0.
 */
DATADOG_API void dd_rum_config_set_session_sample_rate(
    dd_rum_config_t* config, float value
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

/**
 * Adds or updates a global attribute value that will be included with all RUM events
 * emitted hereafter.
 */
DATADOG_API void dd_rum_attribute_set(
    dd_rum_t* rum, const char* name, const dd_attribute_t* value
);

/**
 * Removes a global attribute value, if one has been previously added with the given
 * name.
 */
DATADOG_API void dd_rum_attribute_delete(dd_rum_t* rum, const char* name);

// === RUM sessions ===

/**
 * Explicitly stops the current RUM session, if one is active.
 *
 * Once a session has been explicitly stopped, the next call to dd_rum_start_view(),
 * dd_rum_start_action(), or dd_rum_add_action() will automatically start a new session.
 * If that new session is created in response to an action, the last active view from
 * the previous session will be restarted in the new session.
 */
DATADOG_API void dd_rum_stop_session(dd_rum_t* rum);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_RUM_H
