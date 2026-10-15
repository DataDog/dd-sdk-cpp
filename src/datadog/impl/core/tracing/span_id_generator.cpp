// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "span_id_generator.hpp"

namespace datadog::impl {

DefaultSpanIDGenerator::DefaultSpanIDGenerator()
    : _rng(std::random_device{}()),
      _distribution(
          std::numeric_limits<uint64_t>::min(), std::numeric_limits<uint64_t>::max()
      ) {}

SpanID DefaultSpanIDGenerator::Generate() {
  while (1) {
    SpanID id(_distribution(_rng));

    if (id.IsValid()) {
      return id;
    }
  }
}

}  // namespace datadog::impl
