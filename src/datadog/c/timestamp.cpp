// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/timestamp.h"

#include <cstring>

#include "datadog/impl/core/util/assert.hpp"

extern "C" {

dd_timestamp_t dd_timestamp_ns(int64_t nanoseconds_since_epoch) {
  return nanoseconds_since_epoch;
}

dd_timestamp_t dd_timestamp_ms(int64_t milliseconds_since_epoch) {
  return milliseconds_since_epoch * 1000000;
}

dd_timestamp_t dd_timestamp_seconds(int64_t seconds_since_epoch) {
  return seconds_since_epoch * 1000000000;
}
}
