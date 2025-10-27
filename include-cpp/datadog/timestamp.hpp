// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>

namespace datadog {

/**
 * A duration compatible for comparison and arithmetic with a datadog::Timestamp, stored
 * as a signed int64 count of nanoseconds.
 */
using Duration = std::chrono::nanoseconds;

/**
 * A point in time as measured by the system clock, expressed as a signed int64 count of
 * nanoseconds since the Unix epoch.
 */
using Timestamp = std::chrono::time_point<std::chrono::system_clock, Duration>;

// std::chrono::time_point wraps an underlying count, which is a 64-bit int
static_assert(sizeof(Timestamp) == sizeof(int64_t), "unexpected Timestamp size");

}  // namespace datadog
