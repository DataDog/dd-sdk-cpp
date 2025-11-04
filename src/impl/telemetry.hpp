// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>

#include "datadog/attribute.hpp"
#include "diagnostics.hpp"

namespace datadog::impl {

class ITelemetry {
 public:
  virtual void GenerateDebugEvent(const char* message, const Attribute& attributes) = 0;
  virtual void GenerateErrorEvent(
      const char* message, const char* kind, const char* stack
  ) = 0;
};

class Telemetry final : public ITelemetry {
  DiagnosticLogger local_logger;

 public:
  void Debug(const char* text, const Attribute& attributes = Attribute()) override {
    local_logger.Error(text);
  }
};

}  // namespace datadog::impl
