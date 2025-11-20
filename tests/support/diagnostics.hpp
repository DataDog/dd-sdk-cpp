// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <initializer_list>
#include <string_view>
#include <vector>

#include "datadog/core.h"
#include "datadog/core.hpp"
#include "support/catch.hpp"

using namespace datadog;

/**
 * Collated set of diagnostic messages collected from CoreTestHarness, with a convenient
 * interface for asserting that the expected set of errors/warnings/etc. was received.
 */
struct DiagnosticAsserts {
  std::vector<std::string_view> debug;
  std::vector<std::string_view> status;
  std::vector<std::string_view> warning;
  std::vector<std::string_view> error;

  explicit DiagnosticAsserts(
      const std::vector<dd_diagnostic_message_t>& c_diagnostics,
      const std::vector<DiagnosticMessage>& cpp_diagnostics
  ) {
    for (const dd_diagnostic_message_t& message : c_diagnostics) {
      switch (message.level) {
        case DD_DIAGNOSTIC_LEVEL_DEBUG:
          debug.emplace_back(message.text);
          break;
        case DD_DIAGNOSTIC_LEVEL_STATUS:
          status.emplace_back(message.text);
          break;
        case DD_DIAGNOSTIC_LEVEL_WARNING:
          warning.emplace_back(message.text);
          break;
        case DD_DIAGNOSTIC_LEVEL_ERROR:
          error.emplace_back(message.text);
          break;
      }
    }
    for (const DiagnosticMessage& message : cpp_diagnostics) {
      switch (message.level) {
        case DiagnosticLevel::Debug:
          debug.emplace_back(message.text);
          break;
        case DiagnosticLevel::Status:
          status.emplace_back(message.text);
          break;
        case DiagnosticLevel::Warning:
          warning.emplace_back(message.text);
          break;
        case DiagnosticLevel::Error:
          error.emplace_back(message.text);
          break;
      }
    }
  }

  const DiagnosticAsserts& RequireWarnings(std::vector<std::string_view> want) const {
    REQUIRE(warning == want);
    return *this;
  }

  const DiagnosticAsserts& RequireErrors(std::vector<std::string_view> want) const {
    REQUIRE(error == want);
    return *this;
  }

  const DiagnosticAsserts& RequireNoWarnings() const { return RequireWarnings({}); }

  const DiagnosticAsserts& RequireNoErrors() const { return RequireErrors({}); }
};
