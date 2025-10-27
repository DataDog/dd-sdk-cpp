// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_TIMESTAMP_H
#define DATADOG_INCLUDE_TIMESTAMP_H

#include <inttypes.h>

#include "datadog/api.h"

/**
 * A point in time as measured by the system clock, expressed as a signed int64 count of
 * nanoseconds since the Unix epoch.
 */
typedef int64_t dd_timestamp_t;

#ifdef __cplusplus
extern "C" {
#endif

DATADOG_API dd_timestamp_t dd_timestamp_ns(int64_t nanoseconds_since_epoch);
DATADOG_API dd_timestamp_t dd_timestamp_ms(int64_t milliseconds_since_epoch);
DATADOG_API dd_timestamp_t dd_timestamp_seconds(int64_t seconds_since_epoch);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_TIMESTAMP_H
