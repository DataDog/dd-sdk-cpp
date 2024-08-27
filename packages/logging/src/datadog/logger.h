// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#pragma once

#include <datadog/core.h>

namespace datadog::logging {

using datadog::core::IDatadogCore;

struct LoggerOptions {};

class Logger {
 public:
  Logger(const LoggerOptions& options,
         const std::shared_ptr<IDatadogCore>& core);

  void Log(const std::string_view& message);

 private:
  std::weak_ptr<IDatadogCore> core_;
};

}  // namespace datadog::logging
