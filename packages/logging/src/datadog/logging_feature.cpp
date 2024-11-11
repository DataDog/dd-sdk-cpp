// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/logging_feature.h"

#include "datadog/logger.h"

namespace datadog::logging {

using datadog::core::reporting::Report;
using datadog::core::storage::TLVFileReader;

std::unique_ptr<DatadogLogger> DatadogLogging::CreateLogger(
    const DatadogLogConfiguration& configuration) {
  return std::make_unique<DatadogLogger>(configuration, weak_from_this());
}

Report DatadogLogging::CreateReportFromBatch(TLVFileReader&) const {
  return Report("api/v2/logging");
}

}  // namespace datadog::logging
