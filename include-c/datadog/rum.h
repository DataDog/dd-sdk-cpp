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
DATADOG_API void dd_rum_add_attribute(
    dd_rum_t* rum, const char* name, const dd_attribute_t* value
);

/**
 * Removes a global attribute value, if one has been previously added with the given
 * name.
 */
DATADOG_API void dd_rum_remove_attribute(dd_rum_t* rum, const char* name);

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

// === RUM views ===

/**
 * Starts a new RUM view, recording that the user has navigated to the portion of the
 * application uniquely identified by the given string `key`. `name` is an optional
 * human-readable view name to be used in the Datadog UI; if not specified, the view's
 * key will be used as its name.
 *
 * If no RUM session is currently active, starting a view will implicitly create a new
 * session.
 *
 * When a new view is started, all existing views are implicitly stopped.
 */
DATADOG_API void dd_rum_start_view(dd_rum_t* rum, const char* key, const char* name);

/**
 * Starts a new RUM view, associating an initial set of custom attributes with that
 * view.
 */
DATADOG_API void dd_rum_start_view_obj(
    dd_rum_t* rum, const char* key, const char* name, const dd_attribute_t* attributes
);

/**
 * Adds or updates a custom attribute value stored in the context of the current view.
 *
 * All events produced within the context of a view will include the set of custom
 * attributes formed from both global and view-level attributes, with view attributes
 * taking precedence in the case of name conflicts.
 *
 * View attributes are scoped to the lifetime of the view and do not persist to
 * subsequent views.
 */
DATADOG_API void dd_rum_add_view_attribute(
    dd_rum_t* rum, const char* name, const dd_attribute_t* value
);

/**
 * Removes any custom attribute value stored under the given name in the context of the
 * current view.
 *
 * If the view-level attribute being removed was shadowing a global attribute of the
 * same name, subsequent events will once again use the global attribute value.
 */
DATADOG_API void dd_rum_remove_view_attribute(dd_rum_t* rum, const char* name);

/**
 * Stops any active RUM views that are identified with the given key.
 */
DATADOG_API void dd_rum_stop_view(dd_rum_t* rum, const char* key);

/**
 * Stops any active RUM views that have the given key, and includes the provided set of
 * custom user attributes in the final event sent for that view.
 */
DATADOG_API void dd_rum_stop_view_obj(
    dd_rum_t* rum, const char* key, const dd_attribute_t* attributes
);

// === RUM actions ===

/**
 * Type of user action to be recorded. Action types like 'tap', 'click', 'scroll', and
 * 'swipe' may be used to record user input. An action of type 'custom' records any
 * arbitrary event that occurs in response to the user's interactions with the
 * application.
 */
typedef enum {
  DD_RUM_ACTION_TYPE_TAP,
  DD_RUM_ACTION_TYPE_CLICK,
  DD_RUM_ACTION_TYPE_SCROLL,
  DD_RUM_ACTION_TYPE_SWIPE,
  DD_RUM_ACTION_TYPE_CUSTOM
} dd_rum_action_type_t;

/**
 * Records a discrete user action of the given type in the context of the current view.
 * A name is required, and the provided name will be used to identify the target of the
 * action in the Datadog UI.
 *
 * Discrete actions (i.e. those added via dd_rum_add_action()) are momentary: they do
 * not require an explicit call to dd_rum_stop_action().
 *
 * Discrete actions with type DD_RUM_ACTION_TYPE_CUSTOM are reported immediately, and
 * they may be recorded via dd_rum_add_action() at any time, regardless of whether
 * another action is curently active.
 *
 * Discrete actions of all other types will remain active for at least 100ms prior to
 * being reported, so that any RUM resources or errors occurring immediately after the
 * action may be correlated with the action.
 *
 * If dd_rum_start_resource() calls occur while the action is active, the action may
 * remain active until the corresponding dd_rum_stop_resource() calls are made, even if
 * those resources remain active after the initial 100ms timeout. However, an explicit
 * dd_rum_stop_action() call will always stop the action, regardless of whether it's
 * waiting for resources to complete.
 *
 * Only one action may be active at any given time: if you call dd_rum_add_action() or
 * dd_rum_start_action() while another action is active, the new action will be ignored
 * (with the exception of dd_rum_add_action() with DD_RUM_ACTION_TYPE_CUSTOM as
 * described above).
 */
DATADOG_API void dd_rum_add_action(
    dd_rum_t* rum,
    dd_rum_action_type_t type,
    const char* name,
    dd_attribute_t* attributes
);

/**
 * Records a continuous user action of the given type in the context of the current
 * view. A name is required, and the provided name will be used to identify the target
 * of the action in the Datadog UI.
 *
 * A continuous user action will remain active until dd_rum_stop_action() is called, or
 * until a timeout duration (of at least 10 seconds) has elapsed without a call to
 * dd_rum_stop_action().
 *
 * If dd_rum_start_resource() calls occur while the action is active, the action may
 * remain active until the corresponding dd_rum_stop_resource() calls are made, even if
 * those resources remain active after the initial 10-second timeout. However, an
 * explicit dd_rum_stop_action() call will always stop the action, regardless of whether
 * it's waiting for resources to complete.
 *
 * Only one action may be active at a time. If you call dd_rum_start_action() while
 * another action is already active, regardless of the type you pass to
 * dd_rum_start_action(), the new action will be ignored.
 */
DATADOG_API void dd_rum_start_action(
    dd_rum_t* rum,
    dd_rum_action_type_t type,
    const char* name,
    dd_attribute_t* attributes
);

/**
 * Stops the currently-active action, if any exists in the current view.
 *
 * By convention, the provided `type` value is expected to match the value passed to
 * dd_rum_start_action() for the action that you intend to stop. However, this value is
 * currently ignored.
 *
 * A name value is not required. If provided, the active action will have its name
 * changed to the provided value before it is stopped. If NULL or "", the active action
 * will be stopped without any change to its name.
 *
 * dd_rum_stop_action() will always stop the active action, regardless of whether it was
 * created via dd_rum_add_action() or dd_rum_start_action(), regardless of its type and
 * name, and regardless of whether the action is waiting for associated RUM resources to
 * be stopped.
 */
DATADOG_API void dd_rum_stop_action(
    dd_rum_t* rum,
    dd_rum_action_type_t type,
    const char* name,
    dd_attribute_t* attributes
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_RUM_H
