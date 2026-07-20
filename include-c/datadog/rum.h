// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_RUM_H
#define DATADOG_INCLUDE_RUM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "datadog/api.h"
#include "datadog/attribute.h"
#include "datadog/core.h"
#include "datadog/timestamp.h"
#include "datadog/uuid.h"

#ifdef __cplusplus
extern "C" {
#endif

// === RUM configuration ===

// To enable RUM instrumentation for your application, you must supply the ID for your
// RUM Application. In the Datadog UI, see: Digital Experience -> Real User Monitoring
// -> Manage Applications -> [Your Application] -> SDK Configuration.

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

// You must register RUM as a feature after calling dd_core_create() and before calling
// dd_core_start().

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

// A session represents a single user's interactions with the application over a
// continuous span of time up to 4 hours in length. After 4 hours, or if 15 minutes
// elapses with no user interaction, the session will expire, resulting in a new session
// the next time user interactions are recorded.

// There is no need to manually register session start: the SDK will manage session
// lifecycle automatically in response to dd_rum_start_view(), dd_rum_add_action(), etc.

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

// A view represents a distinct portion of the application (page, screen, section,
// level) with which the user is exclusively interacting with. All RUM actions,
// resources, and errors are recorded in the context of the current view.

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
 *
 * If `attributes` is an object with one or more properties, those custom attribute
 * values will be associated with the new view and attached to all RUM events sent in
 * the context of that view.
 */
DATADOG_API void dd_rum_start_view(
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
 * Stops any active RUM views that have the given key, optionally including the provided
 * set of custom user attributes in the final event sent for that view.
 */
DATADOG_API void dd_rum_stop_view(
    dd_rum_t* rum, const char* key, const dd_attribute_t* attributes
);

// === RUM actions ===

// An action represents a single user interaction in the context of the current view.

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

// === RUM resources ===

// A resource represents a single HTTP request made by the application in the context of
// the current view.

/**
 * HTTP method used to initiate the retrieval of a resource.
 */
typedef enum {
  DD_RUM_RESOURCE_METHOD_GET,
  DD_RUM_RESOURCE_METHOD_HEAD,
  DD_RUM_RESOURCE_METHOD_POST,
  DD_RUM_RESOURCE_METHOD_PUT,
  DD_RUM_RESOURCE_METHOD_DELETE,
  DD_RUM_RESOURCE_METHOD_CONNECT,
  DD_RUM_RESOURCE_METHOD_OPTIONS,
  DD_RUM_RESOURCE_METHOD_TRACE,
  DD_RUM_RESOURCE_METHOD_PATCH
} dd_rum_resource_method_t;

/**
 * Type of resource retrieved; typically inferred from a response's Content-Type.
 */
typedef enum {
  DD_RUM_RESOURCE_TYPE_UNKNOWN,
  DD_RUM_RESOURCE_TYPE_BEACON,
  DD_RUM_RESOURCE_TYPE_FETCH,
  DD_RUM_RESOURCE_TYPE_XHR,
  DD_RUM_RESOURCE_TYPE_DOCUMENT,
  DD_RUM_RESOURCE_TYPE_NATIVE,
  DD_RUM_RESOURCE_TYPE_IMAGE,
  DD_RUM_RESOURCE_TYPE_JS,
  DD_RUM_RESOURCE_TYPE_FONT,
  DD_RUM_RESOURCE_TYPE_CSS,
  DD_RUM_RESOURCE_TYPE_MEDIA,
  DD_RUM_RESOURCE_TYPE_OTHER
} dd_rum_resource_type_t;

/**
 * Records that the application has initiated an HTTP request, causing that request to
 * be tracked as a RUM Resource in the context of the current view.
 *
 * @param key - An arbitrary string that uniquely identifies this specific HTTP request.
 *  Must be unique among all concurrent requests. Must be a valid, non-empty string.
 * @param method - The HTTP request method used.
 * @param url - The request URL. Conventionally, this is an absolute URL. Must be a
 *  valid, non-empty string.
 * @param attributes - An optional set of custom attributes describing the resource,
 *  provided as a dd_attribute_t value with DD_VALUE_TYPE_OBJECT.
 */
DATADOG_API void dd_rum_start_resource(
    dd_rum_t* rum,
    const char* key,
    dd_rum_resource_method_t method,
    const char* url,
    dd_attribute_t* attributes
);

/**
 * Records that an HTTP request has been completed and has received a valid response. A
 * response with a 400-level or 500-level status code is still considered a valid
 * response, provided that the application encountered no errors while processing it.
 *
 * @param key - The unique identifier that was used when the target resource was
 *  started. Must be a valid, non-empty string.
 * @param status_code - The HTTP status code received from the response. If unknown,
 *  supply 0.
 * @param size - The total size of the resource. Conventionally, this is the decoded
 *  size of the response body in bytes; i.e. the response size after decompression,
 *  excluding headers and framing overhead. If unknown, supply -1.
 * @param type - The kind of resource that was retrieved by this request. If unknown,
 *  supply DD_RUM_RESOURCE_TYPE_UNKNOWN.
 * @param attributes - An optional set of custom attributes describing the resource,
 *  provided as a dd_attribute_t value with DD_VALUE_TYPE_OBJECT, to be merged with any
 *  custom attribute values provided when the resource was started.
 */
DATADOG_API void dd_rum_stop_resource(
    dd_rum_t* rum,
    const char* key,
    int32_t status_code,
    int64_t size,
    dd_rum_resource_type_t type,
    dd_attribute_t* attributes
);

/**
 * Records that an HTTP request could not be completed due to an error (and therefore no
 * response was received), or that processing of the response failed due to an error.
 *
 * @param key - The unique identifier that was used when the target resource was
 *  started. Must be a valid, non-empty string.
 * @param error_message - A string describing the error. Should be a valid, non-empty
 *  string.
 * @param error_type - A name describing the error's type. May be omitted.
 * @param error_stack_trace - The full text of a stack trace describing the context for
 *  the error. May be omitted.
 * @param is_network_error - Whether the request failed due to a network connection
 *  issue, such as DNS lookup failure, connection timeout, etc. Used to categorize the
 *  resulting RUM Error in either the "network" or "exception" category.
 * @param status_code - The HTTP status code received from the response. If unknown, or
 *  if no response was received, supply 0.
 * @param attributes - An optional set of custom attributes describing the resource,
 *  provided as a dd_attribute_t value with DD_VALUE_TYPE_OBJECT, to be merged with any
 *  custom attribute values provided when the resource was started.
 */
DATADOG_API void dd_rum_stop_resource_with_error(
    dd_rum_t* rum,
    const char* key,
    const char* error_message,
    const char* error_type,
    const char* error_stack_trace,
    bool is_network_error,
    int32_t status_code,
    dd_attribute_t* attributes
);

// === RUM operations ===

// A operation represents a user-facing workflow (e.g. login, checkout, upload)
// that can span multiple views and is tracked for performance and reliability insights.

/**
 * Describes the reason why a operation failed.
 *
 * This API is in preview and may change in future releases.
 */
typedef enum {
  DD_RUM_FAILURE_REASON_ERROR,
  DD_RUM_FAILURE_REASON_ABANDONED,
  DD_RUM_FAILURE_REASON_OTHER
} dd_rum_failure_reason_t;

/**
 * Records the start of a operation (e.g. login, checkout, upload).
 *
 * This API is in preview and may change in future releases.
 *
 * @param name - The name of the operation. Must be a valid, non-empty string.
 * @param operation_key - An optional key to distinguish parallel instances of the same
 *  operation. Pass NULL to omit.
 * @param attributes - An optional set of custom attributes for this event.
 */
DATADOG_API void dd_rum_start_operation(
    dd_rum_t* rum,
    const char* name,
    const char* operation_key,
    dd_attribute_t* attributes
);

/**
 * Records the successful completion of a operation.
 *
 * This API is in preview and may change in future releases.
 *
 * @param name - The name of the operation. Must be a valid, non-empty string.
 * @param operation_key - An optional key to distinguish parallel instances of the same
 *  operation. Pass NULL to omit.
 * @param attributes - An optional set of custom attributes for this event.
 */
DATADOG_API void dd_rum_succeed_operation(
    dd_rum_t* rum,
    const char* name,
    const char* operation_key,
    dd_attribute_t* attributes
);

/**
 * Records the failure of a operation.
 *
 * This API is in preview and may change in future releases.
 *
 * @param name - The name of the operation. Must be a valid, non-empty string.
 * @param failure_reason - The reason the operation failed.
 * @param operation_key - An optional key to distinguish parallel instances of the same
 *  operation. Pass NULL to omit.
 * @param attributes - An optional set of custom attributes for this event.
 */
DATADOG_API void dd_rum_fail_operation(
    dd_rum_t* rum,
    const char* name,
    dd_rum_failure_reason_t failure_reason,
    const char* operation_key,
    dd_attribute_t* attributes
);

// === RUM errors ===

/**
 * Describes a component of the application from which a RUM error originates.
 */
typedef enum {
  DD_RUM_ERROR_SOURCE_NETWORK,
  DD_RUM_ERROR_SOURCE_SOURCE,
  DD_RUM_ERROR_SOURCE_CONSOLE,
  DD_RUM_ERROR_SOURCE_LOGGER,
  DD_RUM_ERROR_SOURCE_AGENT,
  DD_RUM_ERROR_SOURCE_WEBVIEW,
  DD_RUM_ERROR_SOURCE_CUSTOM,
  DD_RUM_ERROR_SOURCE_REPORT
} dd_rum_error_source_t;

/**
 * Records that the application has encountered an error in the context of the current
 * view.
 *
 * @param source - The source of the error. If in doubt: 'source' covers errors due to
 *  bugs in the application's source; 'network' covers network issues; and 'custom' is
 *  resonable catch-all.
 * @param message - A string describing the error. Should be a valid, non-empty string.
 * @param type - A name describing the error's type. May be omitted.
 * @param stack_trace - The full text of a stack trace describing the context for the
 *  error. May be omitted.
 * @param attributes - An optional set of custom attributes describing the error,
 *  provided as a dd_attribute_t value with DD_VALUE_TYPE_OBJECT.
 */
DATADOG_API void dd_rum_add_error(
    dd_rum_t* rum,
    dd_rum_error_source_t source,
    const char* message,
    const char* type,
    const char* stack_trace,
    dd_attribute_t* attributes
);

// === RUM long tasks ===

/**
 * Records that the application encountered a long task (a period during which the main
 * thread was blocked for an extended duration) in the context of the current view.
 *
 * @param duration - The duration of the long task. Must be positive.
 * @param attributes - An optional set of custom attributes describing the long task,
 *  provided as a dd_attribute_t value with DD_VALUE_TYPE_OBJECT.
 */
DATADOG_API void dd_rum_add_long_task(
    dd_rum_t* rum, dd_duration_t duration, dd_attribute_t* attributes
);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_RUM_H
