#pragma once

#include <memory>

#include "datadog/logging.h"
#include "datadog/logging.hpp"

namespace datadog::impl {
class Logger;
class Logging;
}  // namespace datadog::impl

struct dd_logger_config {
  datadog::LoggerConfig cpp_config;
};

struct dd_logger {
  std::unique_ptr<datadog::impl::Logger> impl;
};

struct dd_logging {
  std::shared_ptr<datadog::impl::Logging> impl;
};
