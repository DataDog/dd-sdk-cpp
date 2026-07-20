// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/duration.h"

extern "C" {

dd_duration_t dd_duration_ns(int64_t nanoseconds) { return nanoseconds; }

dd_duration_t dd_duration_ms(int64_t milliseconds) { return milliseconds * 1000000; }

dd_duration_t dd_duration_seconds(int64_t seconds) { return seconds * 1000000000; }
}
