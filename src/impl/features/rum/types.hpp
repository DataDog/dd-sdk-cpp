// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/rum.h"
#include "datadog/rum.hpp"

namespace datadog {

inline RumConfig RumConfig_FromC(const dd_rum_config_t& config) {
  // Convert all of the C struct's string values to std::string_view safely
  std::string_view application_id = config.application_id ? config.application_id : "";

  // Initialize a C++ config struct from our input values
  return RumConfig(application_id);
}

}  // namespace datadog
