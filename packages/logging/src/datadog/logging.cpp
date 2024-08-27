// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/logging.h"

namespace datadog::logging {

LoggingFeature::LoggingFeature(const std::shared_ptr<IDatadogCore>& core)
    : core_(core) {}

std::unique_ptr<Logger> LoggingFeature::CreateLogger(
    const LoggerOptions& options) {
    return std::make_unique<Logger>(options, core_.lock());
}

}  // namespace datadog::logging
