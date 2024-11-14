// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include "datadog/core.h"
#include "datadog/internal/core_internal.h"

#include <catch2/trompeloeil.hpp>

namespace datadog::core::mocks {

using datadog::core::CoreMessage;
using datadog::core::DatadogCore;
using datadog::core::FeatureId;
using datadog::core::internal::DatadogCoreInternal;

class MockDatadogCore : public DatadogCore {
 public:
  MockDatadogCore(Allow allow) : DatadogCore(allow) {}

  MAKE_MOCK0(GetNow, Nanoseconds(), const override);
  MAKE_MOCK0(Shutdown, void(), override);
  MAKE_MOCK1(FeatureExists, bool(FeatureId), const override);
  MAKE_MOCK0(GetContext, const internal::CoreContext&(), const override);
  MAKE_MOCK1(SendMessage, void(CoreMessage&&), override);
  MAKE_MOCK2(RegisterFeature,
             void(FeatureId, const std::shared_ptr<DatadogFeature>&),
             override);

  static std::shared_ptr<MockDatadogCore> Create() {
    return std::make_shared<MockDatadogCore>(Allow{});
  }
};

}  // namespace datadog::core::mocks
