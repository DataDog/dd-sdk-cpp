// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <utility>

#include "common/global.hpp"
#include "datadog/core.h"
#include "datadog/core.hpp"

dd_core_config_t InitConfigForC(const GlobalOptions& opts);

datadog::CoreConfig InitConfigForCpp(const GlobalOptions& opts);

const dd_core_config_t& ParseConfigForC(const void* config);

const datadog::CoreConfig& ParseConfigForCpp(const void* config);

template <typename T>
const T& ParseBenchmarkOptions(const void* b) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *(reinterpret_cast<const T*>(b));
}
