// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#ifndef DATADOG_INCLUDE_DURATION_H
#define DATADOG_INCLUDE_DURATION_H

#include <inttypes.h>

#include "datadog/api.h"

/**
 * A span of time, expressed as a signed int64 count of nanoseconds.
 */
typedef int64_t dd_duration_t;

#ifdef __cplusplus
extern "C" {
#endif

DATADOG_API dd_duration_t dd_duration_ns(int64_t nanoseconds);
DATADOG_API dd_duration_t dd_duration_ms(int64_t milliseconds);
DATADOG_API dd_duration_t dd_duration_seconds(int64_t seconds);

#ifdef __cplusplus
}
#endif

#endif  // DATADOG_INCLUDE_DURATION_H
