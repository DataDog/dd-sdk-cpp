// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/logger.h"

namespace datadog::logging {

using datadog::core::IDatadogCore;

Logger::Logger(const LoggerOptions& options,
               const std::shared_ptr<IDatadogCore>& core)
    : core_(core) {}

void Logger::Log(const std::string_view& message) {}

}  // namespace datadog::logging
