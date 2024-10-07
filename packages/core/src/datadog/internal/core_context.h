// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <string>

namespace datadog::core::internal {

// Properties that were configured during Core initialization
struct CoreContext {
  std::string service;
  std::string env;
  std::string_view application_version;
};

}  // namespace datadog::core::internal
