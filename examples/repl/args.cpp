// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "repl/args.hpp"

#include <optional>

namespace {

std::optional<std::string_view> check_arg(
    std::string_view option, int& i, int argc, char* argv[]
) {
  std::string_view arg = argv[i];
  if (arg == option && i + 1 < argc) {
    return argv[++i];
  }
  size_t delim_pos = option.size();
  if (arg.find(option) == 0 && arg.size() > delim_pos && arg[delim_pos] == '=') {
    return arg.substr(delim_pos + 1, option.size() - delim_pos - 1);
  }
  return std::nullopt;
}

}  // namespace

Args Args::Parse(int argc, char* argv[]) {
  Args result{};

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--autostart") {
      result.autostart = true;
      continue;
    }
    if (arg == "--abort-on-error") {
      result.abort_on_error = true;
      continue;
    }
    if (arg == "--abort-on-warning") {
      result.abort_on_warning = true;
      result.abort_on_error = true;
      continue;
    }

    if (auto val = check_arg("--client-token", i, argc, argv)) {
      result.client_token = *val;
      continue;
    }
    if (auto val = check_arg("--service", i, argc, argv)) {
      result.service = *val;
      continue;
    }
    if (auto val = check_arg("--env", i, argc, argv)) {
      result.env = *val;
      continue;
    }
    if (auto val = check_arg("--rum-application-id", i, argc, argv)) {
      result.rum_application_id = *val;
      continue;
    }
    if (auto val = check_arg("--custom-endpoint-url", i, argc, argv)) {
      result.custom_endpoint_url = *val;
      continue;
    }
  }

  return result;
}
