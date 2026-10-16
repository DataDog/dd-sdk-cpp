// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <random>

#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/types/tracing/trace_id.hpp"

namespace datadog::impl {

/** Interface for a TraceID generator. */
class ITraceIDGenerator {
 public:
  virtual ~ITraceIDGenerator() = default;

  /** Generate a TraceID. */
  virtual TraceID Generate() = 0;

 protected:
  ITraceIDGenerator() = default;
};

/**
 * Default TraceID generator.
 *
 * This generator generates TraceIDs following this method:
 * - The most significant 32 bits are the system timestamp (in seconds) at the
 * moment of the ID generation;
 * - The following 32 bits are 0;
 * - The least significative 64 bit are random, uniformely distributed.
 */
class DefaultTraceIDGenerator final : public ITraceIDGenerator {
 public:
  DefaultTraceIDGenerator(const platform::IClock& clock);
  DefaultTraceIDGenerator(const DefaultTraceIDGenerator&) = delete;
  DefaultTraceIDGenerator& operator=(const DefaultTraceIDGenerator&) = delete;
  DefaultTraceIDGenerator(DefaultTraceIDGenerator&&) = delete;
  DefaultTraceIDGenerator& operator=(DefaultTraceIDGenerator&&) = delete;
  virtual ~DefaultTraceIDGenerator() override = default;

  TraceID Generate() override;

 private:
  const platform::IClock& _clock;
  std::mt19937 _rng;
  std::uniform_int_distribution<uint64_t> _distribution;
};

}  // namespace datadog::impl
