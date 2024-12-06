// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <string>

namespace datadog::core::internal {

// Properties that were configured during Core initialization
struct CoreContext {
  std::string client_token;
  std::string service;
  std::string env;
  std::string application_version;
  std::string source;
};

}  // namespace datadog::core::internal
