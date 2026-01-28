// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/platform/system_info.hpp"

using namespace datadog;

/**
 * Mock implementation of ISystemInfo. Returns hardcoded test values.
 */
class MockSystemInfo : public platform::ISystemInfo {
 public:
  platform::OsInfo os_info;

  MockSystemInfo() : os_info{"MockOS", "1.0.0", "12345", "1"} {}

  const platform::OsInfo& GetOsInfo() const override { return os_info; }
};
