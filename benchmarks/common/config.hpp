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
