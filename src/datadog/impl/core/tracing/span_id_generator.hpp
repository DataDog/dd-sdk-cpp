// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <random>

#include "datadog/impl/types/tracing/span_id.hpp"

namespace datadog::impl {

/** Interface for a SpanID generator. */
class ISpanIdGenerator {
 public:
  virtual ~ISpanIdGenerator() = default;

  /** Generate a SpanID. */
  virtual SpanID Generate() = 0;

 protected:
  ISpanIdGenerator() = default;
};

/**
 * Default SpanID generator.
 *
 * This generator generates random, uniformely distributed SpanIDs.
 */
class DefaultSpanIDGenerator final : public ISpanIdGenerator {
 public:
  DefaultSpanIDGenerator();
  DefaultSpanIDGenerator(const DefaultSpanIDGenerator&) = delete;
  DefaultSpanIDGenerator& operator=(const DefaultSpanIDGenerator&) = delete;
  DefaultSpanIDGenerator(DefaultSpanIDGenerator&&) = delete;
  DefaultSpanIDGenerator& operator=(DefaultSpanIDGenerator&&) = delete;
  virtual ~DefaultSpanIDGenerator() override = default;

  /** Generate a random SpanID. */
  SpanID Generate() override;

 private:
  std::mt19937 _rng;
  std::uniform_int_distribution<uint64_t> _distribution;
};

}  // namespace datadog::impl
