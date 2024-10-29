// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include "datadog/core.h"

#include <catch2/trompeloeil.hpp>

namespace datadog::core::mocks {

using datadog::core::CoreMessage;
using datadog::core::IDatadogCore;

class MockDatadogCore : public IDatadogCore {
 public:
  MAKE_MOCK0(GetNow, Nanoseconds(), const override);
  MAKE_MOCK0(GetContext, const internal::CoreContext&(), const override);
  MAKE_MOCK1(SendMessage, void(CoreMessage&&), override);
  MAKE_MOCK0(Shutdown, void(), override);
};

}  // namespace datadog::core::mocks
