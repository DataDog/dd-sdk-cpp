// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "test.h"

#include "datadog/logger.h"

#include <mock_core.h>

namespace {

using datadog::core::DatadogCoreConfiguration;
using datadog::core::mocks::MockDatadogCore;
using datadog::logging::Logger;
using datadog::logging::LoggerConfiguration;

TEST_CASE("M send data to core W log", "[logging]") {
  // Given
  auto mock_core = MockDatadogCore::Create(DatadogCoreConfiguration());

  LoggerConfiguration options;
  Logger logger(options, mock_core);

  // When
  logger.Debug("my message");

  // Then
}

}  // namespace
