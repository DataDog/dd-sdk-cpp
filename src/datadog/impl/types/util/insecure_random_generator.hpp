// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>

#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

/**
 * Generates uniformly distributed random integer values.
 *
 * This generator is not cryptographically secure. It must not be used to generate
 * secrets, security tokens, cryptographic nonces, or other security-sensitive values.
 *
 * @tparam IntType An integer type supported by `std::uniform_int_distribution`.
 * Using any other type results in undefined behavior.
 */
template <typename IntType>
class InsecureRandomGenerator final {
 public:
  explicit InsecureRandomGenerator(
      IntType minimum = std::numeric_limits<IntType>::min(),
      IntType maximum = std::numeric_limits<IntType>::max()
  )
      : _engine(CreateEngine()), _distribution(CreateDistribution(minimum, maximum)) {}

  InsecureRandomGenerator(const InsecureRandomGenerator&) = delete;
  InsecureRandomGenerator& operator=(const InsecureRandomGenerator&) = delete;
  InsecureRandomGenerator(InsecureRandomGenerator&&) = delete;
  InsecureRandomGenerator& operator=(InsecureRandomGenerator&&) = delete;
  ~InsecureRandomGenerator() = default;

  /** Generate the next value within the configured inclusive range. */
  IntType NextValue() { return _distribution(_engine); }

 private:
  static std::mt19937_64 CreateEngine() {
    constexpr std::size_t SEED_WORD_COUNT = 4;

    std::random_device random_device;
    std::array<std::random_device::result_type, SEED_WORD_COUNT> seed_data{};
    for (auto& value : seed_data) {
      value = random_device();
    }

    std::seed_seq seed(seed_data.cbegin(), seed_data.cend());
    return std::mt19937_64(seed);
  }

  static std::uniform_int_distribution<IntType> CreateDistribution(
      IntType minimum, IntType maximum
  ) {
    DATADOG_ASSERT(minimum <= maximum, "minimum random value must not exceed maximum");

    return std::uniform_int_distribution<IntType>(minimum, maximum);
  }

  std::mt19937_64 _engine;
  std::uniform_int_distribution<IntType> _distribution;
};

using InsecureRandomUInt64Generator = InsecureRandomGenerator<uint64_t>;

}  // namespace datadog::impl
