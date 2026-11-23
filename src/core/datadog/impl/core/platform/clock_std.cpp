// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>

#include "datadog/impl/core/platform/clock.hpp"

namespace datadog::platform {

/**
 * Implements IClock by sampling std::chrono::system_clock.
 */
class StdClock final : public IClock {
 public:
  Timestamp Now() const final { return std::chrono::system_clock::now(); }
};

std::unique_ptr<IClock> Clock::Init() { return std::make_unique<StdClock>(); }

}  // namespace datadog::platform
