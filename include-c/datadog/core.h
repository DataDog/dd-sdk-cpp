// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_CORE_H
#define DATADOG_INCLUDE_CORE_H

#include <stdbool.h>
#include <stddef.h>

#include "datadog/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DD_TRACKING_CONSENT_GRANTED,
  DD_TRACKING_CONSENT_NOT_GRANTED,
  DD_TRACKING_CONSENT_PENDING,
} dd_tracking_consent_t;

typedef enum {
  DD_SITE_US1,
  DD_SITE_US3,
  DD_SITE_US5,
  DD_SITE_EU1,
  DD_SITE_AP1,
  DD_SITE_AP2,
  DD_SITE_US1_FED,
} dd_site_t;

typedef enum {
  DD_BATCH_SIZE_SMALL,
  DD_BATCH_SIZE_MEDIUM,
  DD_BATCH_SIZE_LARGE,
} dd_batch_size_t;

typedef enum {
  DD_UPLOAD_FREQUENCY_FREQUENT,
  DD_UPLOAD_FREQUENCY_AVERAGE,
  DD_UPLOAD_FREQUENCY_RARE,
} dd_upload_frequency_t;

typedef enum {
  DD_BATCH_PROCESSING_LEVEL_LOW,
  DD_BATCH_PROCESSING_LEVEL_MEDIUM,
  DD_BATCH_PROCESSING_LEVEL_HIGH,
} dd_batch_processing_level_t;

typedef struct dd_core_config {
  dd_tracking_consent_t tracking_consent;
  dd_site_t datadog_site;
  const char* client_token;
  const char* service;
  const char* env;
  const char* application_version;
  dd_batch_size_t batch_size;
  dd_upload_frequency_t upload_frequency;
  dd_batch_processing_level_t batch_processing_level;
  size_t num_http_requests_per_feature_to_flush_on_stop;
  const char* custom_endpoint_url;
} dd_core_config_t;

typedef struct dd_core dd_core_t;

DATADOG_API dd_core_t* dd_core_create(const dd_core_config_t* config);
DATADOG_API void dd_core_destroy(dd_core_t* core);

DATADOG_API bool dd_core_start(dd_core_t* core);
DATADOG_API void dd_core_stop(dd_core_t* core);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_CORE_H
