// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <fstream>
#include <memory>

#include "datadog.hpp"

static constexpr const char* DEFAULT_SERVICE = "dd-sdk-cpp-repl";
static constexpr const char* DEFAULT_ENV = "development";

struct State {
  // Script execution state: script files loaded via 'source <filename>' are pushed onto
  // this stack and processed to completion before we return to reading from stdin
  std::array<std::ifstream, 8> files;
  size_t num_files{0};

  // SDK config (any changes made after create-core do nothing)
  datadog::CoreConfig config{"", DEFAULT_SERVICE, DEFAULT_ENV};
  datadog::RumConfig rum_config{datadog::UUID::Zero};
  bool has_client_token{false};
  bool has_rum_application_id{false};
  datadog::UUID application_id;

  // Core API
  std::shared_ptr<datadog::Core> core;
  bool started{false};

  // Logging API
  std::shared_ptr<datadog::Logging> logging;
  std::array<std::shared_ptr<datadog::Logger>, 16> loggers;
  size_t num_loggers{0};

  // Rum API
  std::shared_ptr<datadog::Rum> rum;
};
