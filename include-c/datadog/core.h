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
  dd_tracking_consent_t tracking_consent;
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
void dd_core_config_init(
    dd_core_config_t* config,
    const char* client_token,
    const char* service,
    const char* env
);

/**
 * Sets the tracking consent value used on SDK startup. Defaults to PENDING.
 *
 * If the user's tracking consent changes after the SDK is initialized, call
 * dd_core_set_tracking_consent() to update it at runtime.
 */
void dd_core_config_set_initial_tracking_consent(
    dd_core_config_t* config, dd_tracking_consent_t value
);

/**
 * Sets the site (i.e. Datadog datacenter) where data for your organization is stored.
 * Defaults to us1.
 */
void dd_core_config_set_site(dd_core_config_t* config, dd_site_t value);

/**
 * Sets the client token value, overriding the value passed to dd_core_config_init().
 */
void dd_core_config_set_client_token(dd_core_config_t* config, const char* value);

/**
 * Sets the 'service' value, overriding the value passed to dd_core_config_init().
 */
void dd_core_config_set_service(dd_core_config_t* config, const char* value);

/**
 * Sets the 'env' value, overriding the value passed to dd_core_config_init().
 */
void dd_core_config_set_env(dd_core_config_t* config, const char* value);

/**
 * Sets the 'version' value, identifying the version of your applicating that's being
 * monitored.
 */
void dd_core_config_set_application_version(
    dd_core_config_t* config, const char* value
);

/**
 * Configures the SDK's batch size, which informs how quickly it will consider a batch
 * of event data ready for upload.
 */
void dd_core_config_set_batch_size(dd_core_config_t* config, dd_batch_size_t value);

/**
 * Configures the SDK's upload frequency, which informs how frequently it will check for
 * new batches of events to upload.
 */
void dd_core_config_set_upload_frequency(
    dd_core_config_t* config, dd_upload_frequency_t value
);

/**
 * Configures the SDK's batch processing level, which limits the number of batches that
 * will be uploaded in a given upload cycle.
 */
void dd_core_config_set_batch_processing_level(
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
