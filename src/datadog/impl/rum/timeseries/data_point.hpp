// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/timestamp.hpp"

namespace datadog::impl {

/**
 * A single sampled value, together with the time at which it was sampled.
 */
template <typename T>
struct DataPoint final {
  Timestamp timestamp;
  T value;
};

}  // namespace datadog::impl
