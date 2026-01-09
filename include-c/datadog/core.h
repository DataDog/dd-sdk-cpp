// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_CORE_H
#define DATADOG_INCLUDE_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "datadog/api.h"

// These values establish the size of string buffers in the C API; they do not imply
// that the Datadog platform imposes any such limits
#define DATADOG_MAX_SERVICE_NAME_LEN 127

#ifdef __cplusplus
extern "C" {
#endif

// === Diagnostic logging ===

/**
 * Severity of a diagnostic message emitted by the SDK.
 */
typedef enum {
  DD_DIAGNOSTIC_LEVEL_DEBUG,
  DD_DIAGNOSTIC_LEVEL_STATUS,
  DD_DIAGNOSTIC_LEVEL_WARNING,
  DD_DIAGNOSTIC_LEVEL_ERROR
} dd_diagnostic_level_t;

/**
 * A single message emitted by the SDK to signal an error or status update. By default,
 * diagnostic messages with a status of 'warning' or 'error' will be printed to stderr.
 *
 * Use dd_core_config_set_diagnostic_threshold() to change the threshold for diagnostic
 * messages: at DD_DIAGNOSTIC_LEVEL_DEBUG, all messages will be emitted; at
 * DD_DIAGNOSTIC_LEVEL_ERROR, only errors will be emitted.
 *
 * Use dd_core_config_set_diagnostic_handler() to specify how emitted messages should be
 * handled. Supply your own callback to override the default behavior of printing to
 * stderr; supply NULL to entirely suppress all diagnostic output.
 *
 * Note that message->text is only valid during the handler invocation. If you need to
 * store the text of message persistently, you must make a copy.
 */
typedef struct dd_diagnostic_message {
  dd_diagnostic_level_t level;
  const char* text;
} dd_diagnostic_message_t;

/**
 * Callback function used to handle a single diagnostic message emitted by the SDK. If
 * you supply a value to dd_core_config_set_diagnostic_handler_userdata(), that value
 * will be passed as `userdata`; otherwise `userdata` will be NULL.
 */
typedef void (*dd_diagnostic_handler_t)(
    const dd_diagnostic_message_t* message, void* userdata
);

/**
 * Default dd_diagnostic_handler_t implementation: prints all messages to stderr,
 * prefixed with '[DATADOG <level>]'.
 */
extern void dd_stderr_diagnostic_handler(const dd_diagnostic_message_t* message, void*);

// === SDK configuration ===

/**
 * Indicates whether the end user has consented to tracking, as determined by your
 * application.
 */
typedef enum {
  DD_TRACKING_CONSENT_GRANTED,
  DD_TRACKING_CONSENT_NOT_GRANTED,
  DD_TRACKING_CONSENT_PENDING,
} dd_tracking_consent_t;

/**
 * The Datadog datacenter in which your organization's data is stored.
 */
typedef enum {
  DD_SITE_US1,
  DD_SITE_US3,
  DD_SITE_US5,
  DD_SITE_EU1,
  DD_SITE_AP1,
  DD_SITE_AP2,
  DD_SITE_US1_FED,
} dd_site_t;

/**
 * Determines how long the SDK will accumulate events in a single batch before releasing
 * that batch to be processed in the next upload cycle.
 */
typedef enum {
  DD_BATCH_SIZE_SMALL,
  DD_BATCH_SIZE_MEDIUM,
  DD_BATCH_SIZE_LARGE,
} dd_batch_size_t;

/**
 * Determines how often upload cycles occur for any given feature.
 */
typedef enum {
  DD_UPLOAD_FREQUENCY_FREQUENT,
  DD_UPLOAD_FREQUENCY_AVERAGE,
  DD_UPLOAD_FREQUENCY_RARE,
} dd_upload_frequency_t;

/**
 * Determines the maximum number of batches that may be processed and uploaded for a
 * given feature within a single upload cycle.
 */
typedef enum {
  DD_BATCH_PROCESSING_LEVEL_LOW,
  DD_BATCH_PROCESSING_LEVEL_MEDIUM,
  DD_BATCH_PROCESSING_LEVEL_HIGH,
} dd_batch_processing_level_t;

/**
 * FOR INTERNAL USE ONLY - These values are not part of the public API and may change
 * without notice. Do not use in production code.
 */
typedef struct dd_internal_options {
  bool flush_http_requests_on_stop;
  const char* custom_endpoint_url;
} dd_internal_options_t;

/**
 * Top-level configuration options for the Datadog SDK. Initialize with
 * dd_core_config_init(), then call dd_core_config_set_<value>().
 */
typedef struct dd_core_config {
  uint32_t version;
  dd_diagnostic_handler_t diagnostic_handler;
  void* diagnostic_handler_userdata;
  dd_diagnostic_level_t diagnostic_threshold;
  dd_tracking_consent_t tracking_consent;
  char event_storage_location[512];
  dd_site_t site;
  const char* client_token;
  const char* service;
  const char* env;
  const char* application_version;
  dd_batch_size_t batch_size;
  dd_upload_frequency_t upload_frequency;
  dd_batch_processing_level_t batch_processing_level;
  dd_internal_options_t internal_options;
} dd_core_config_t;

/**
 * Initializes a new dd_core_config_t with the set of values that are required for the
 * SDK to function.
 *
 * @param client_token The client token associated with your application.
 * @param service The name of the application, service, or component being monitored.
 * @param in_env The environment in which this application is running, e.g. 'prod',
 *  'dev', 'staging', 'testing', etc.
 */
DATADOG_API void dd_core_config_init(
    dd_core_config_t* config,
    const char* client_token,
    const char* service,
    const char* env
);

/**
 * Supplies a callback function that will be invoked whenever the SDK emits a diagnostic
 * message whose level meets or exceeds the configured diagnostic threshold.
 *
 * The default handler is dd_stderr_diagnostic_handler, which prints to stderr. If this
 * value is set to NULL, all diagnostic messages will be silently dropped.
 *
 * The SDK may call the provided handler function from any thread, without
 * synchronization.
 */
DATADOG_API void dd_core_config_set_diagnostic_handler(
    dd_core_config_t* config, dd_diagnostic_handler_t value
);

/**
 * Sets an arbitrary value that will be supplied to all invocations of the diagnostic
 * handler callback. The default handler (dd_stderr_diagnostic_handler) will never read
 * this value.
 */
DATADOG_API void dd_core_config_set_diagnostic_handler_userdata(
    dd_core_config_t* config, void* value
);

/**
 * Sets the threshold for diagnostic logging: any message whose level meets or exceeds
 * this value will be passed to the configured diagnostic handler callback.
 */
DATADOG_API void dd_core_config_set_diagnostic_threshold(
    dd_core_config_t* config, dd_diagnostic_level_t value
);

/**
 * Sets the tracking consent value used on SDK startup. Defaults to PENDING.
 *
 * If the user's tracking consent changes after the SDK is initialized, call
 * dd_core_set_tracking_consent() to update it at runtime.
 */
DATADOG_API void dd_core_config_set_initial_tracking_consent(
    dd_core_config_t* config, dd_tracking_consent_t value
);

/**
 * Sets the directory path where the Datadog SDK will create a subdirectory to store all
 * of the transient data it creates during normal operation.
 *
 * The directory you provide must be a location that is unique to your application,
 * where the current process will be permitted to create directories, write files, and
 * delete files. No shell expansions (e.g. ~, environment variables) are performed.
 *
 * Upon SDK start, the SDK will attempt to create a .datadog/ subdirectory within the
 * event storage location you provided. If unsuccessful, the SDK will print a diagnostic
 * error and fail to start.
 *
 * During normal operation, the SDK will assume exclusive ownership of all files within
 * the .datadog/ directory; freely creating and deleting files as needed. It will NOT
 * read or modify any other path within the event storage location.
 *
 * It is highly recommended that you explicitly specify an event storage location when
 * configuring an instance of the SDK. If you do not, the SDK will print a warning, but
 * it will attempt to create a .datadog/ subdirectory within the working directory for
 * the current process. If you prefer to use the current working directory, explicitly
 * configure the SDK with dd_core_config_set_event_storage_location(&config, ".").
 */
DATADOG_API void dd_core_config_set_event_storage_location(
    dd_core_config_t* config, const char* value
);

/**
 * Sets the site (i.e. Datadog datacenter) where data for your organization is stored.
 * Defaults to us1.
 */
DATADOG_API void dd_core_config_set_site(dd_core_config_t* config, dd_site_t value);

/**
 * Sets the client token value, overriding the value passed to dd_core_config_init().
 */
DATADOG_API void dd_core_config_set_client_token(
    dd_core_config_t* config, const char* value
);

/**
 * Sets the 'service' value, overriding the value passed to dd_core_config_init().
 */
DATADOG_API void dd_core_config_set_service(
    dd_core_config_t* config, const char* value
);

/**
 * Sets the 'env' value, overriding the value passed to dd_core_config_init().
 */
DATADOG_API void dd_core_config_set_env(dd_core_config_t* config, const char* value);

/**
 * Sets the 'version' value, identifying the version of your applicating that's being
 * monitored.
 */
DATADOG_API void dd_core_config_set_application_version(
    dd_core_config_t* config, const char* value
);

/**
 * Configures the SDK's batch size, which informs how quickly it will consider a batch
 * of event data ready for upload.
 */
DATADOG_API void dd_core_config_set_batch_size(
    dd_core_config_t* config, dd_batch_size_t value
);

/**
 * Configures the SDK's upload frequency, which informs how frequently it will check for
 * new batches of events to upload.
 */
DATADOG_API void dd_core_config_set_upload_frequency(
    dd_core_config_t* config, dd_upload_frequency_t value
);

/**
 * Configures the SDK's batch processing level, which limits the number of batches that
 * will be uploaded in a given upload cycle.
 */
DATADOG_API void dd_core_config_set_batch_processing_level(
    dd_core_config_t* config, dd_batch_processing_level_t value
);

// === SDK Core ===

/**
 * Top-level interface to the Datadog SDK.
 *
 * Call dd_core_create() to create a new core, register your desired set of features on
 * that core (e.g. dd_logging_init(core)), and then call dd_core_start() to begin the
 * SDK's background processing.
 */
typedef struct dd_core dd_core_t;

DATADOG_API dd_core_t* dd_core_create(const dd_core_config_t* config);
DATADOG_API void dd_core_destroy(dd_core_t* core);

DATADOG_API void dd_core_set_tracking_consent(
    dd_core_t* core, dd_tracking_consent_t value
);

DATADOG_API bool dd_core_start(dd_core_t* core);
DATADOG_API void dd_core_stop(dd_core_t* core);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_CORE_H
