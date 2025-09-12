#pragma once

#include <memory>

#include "datadog/rum.h"
#include "datadog/rum.hpp"

namespace datadog::impl {
class Rum;
}  // namespace datadog::impl

struct dd_rum_config {
  datadog::RumConfig cpp_config;
};

struct dd_rum {
  std::shared_ptr<datadog::impl::Rum> impl;
};
