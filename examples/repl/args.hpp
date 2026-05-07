// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

struct Args {
  // Config options for REPL behavior
  bool autostart{false};
  bool abort_on_error{false};
  bool abort_on_warning{false};

  // SDK config options that may be specified via command-line
  std::string_view client_token;
  std::string_view service;
  std::string_view env;
  std::string_view rum_application_id;
  std::string_view custom_endpoint_url;
  std::string_view storage_path;

  static Args Parse(int argc, char* argv[]);
};
