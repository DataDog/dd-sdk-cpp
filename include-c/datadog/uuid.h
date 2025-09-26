// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_UUID_H
#define DATADOG_INCLUDE_UUID_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#include "datadog/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dd_uuid {
  uint8_t bytes[16];
} dd_uuid_t;

DATADOG_API void dd_uuid_init(dd_uuid_t* value);
DATADOG_API void dd_uuid_random(dd_uuid_t* value);
DATADOG_API void dd_uuid_set(dd_uuid_t* value, const uint8_t bytes[16]);
DATADOG_API bool dd_uuid_parse(dd_uuid_t* value, const char* s);
DATADOG_API void dd_uuid_to_string(const dd_uuid_t* value, char out_s[37]);
DATADOG_API bool dd_uuid_is_zero(const dd_uuid_t* value);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_UUID_H
