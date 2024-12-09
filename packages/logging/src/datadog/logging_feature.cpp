// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include "datadog/logging_feature.h"

#include <sstream>

#include "datadog/internal/sdk_version.h"
#include "datadog/logger.h"
#include "logging_feature.h"

namespace datadog::logging {

using namespace std::string_view_literals;

using datadog::core::internal::CoreContext;
using datadog::core::internal::kSdkVersion;
using datadog::core::reporting::Report;
using datadog::core::storage::DatadogFileStatus;
using datadog::core::storage::TLVBlock;
using datadog::core::storage::TLVFileReader;

std::unique_ptr<DatadogLogger> DatadogLogging::CreateLogger(
    const DatadogLogConfiguration& configuration) {
  return std::make_unique<DatadogLogger>(configuration, weak_from_this());
}

void DatadogLogging::AddAttribute(std::string_view name,
                                  const core::DatadogAttribute& value) {
  global_attributes_.SetMember(name, value);
}

Report DatadogLogging::CreateReportFromBatch(const CoreContext& context,
                                             TLVFileReader& batch) const {
  auto report = Report{"/api/v2/logs"};

  report.SetHeader("Content-Type", "application/json");
  report.SetHeader("DD-API-KEY", context.client_token);
  report.SetHeader("DD-EVP-ORIGIN", context.source);
  report.SetHeader("DD-ORIGIN-VERSION", kSdkVersion);

  // TODO(jeff.ward): Avoid this allocation
  std::stringstream source;
  source << "ddsource="sv << context.source;
  report.AddQuery(source.str());

  // TODO(RUM-7415): setup user agent header

  std::stringstream data_buffer;
  data_buffer << "["sv;

  TLVBlock block;
  bool first = true;
  while (DatadogFileStatus::Ok == batch.ReadBlock(block)) {
    if (!first) {
      data_buffer << ","sv;
    }
    first = false;
    data_buffer << std::string_view{block.data.data(), block.data.size()};
  }
  data_buffer << "]"sv;
  report.SetBody(data_buffer.str());

  return report;
}

}  // namespace datadog::logging
