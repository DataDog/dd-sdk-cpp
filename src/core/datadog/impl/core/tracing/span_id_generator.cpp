// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "span_id_generator.hpp"

namespace datadog::impl {

DefaultSpanIDGenerator::DefaultSpanIDGenerator()
    // Intentionally capped at INT64_MAX since some tracers cannot handle
    // the most significant bit set.
    : _random_generator(1, INT64_MAX) {}

SpanID DefaultSpanIDGenerator::Generate() {
  return SpanID(_random_generator.NextValue());
}

}  // namespace datadog::impl
