// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/logger.h"

#include <sstream>

#include "datadog/log_event.h"
#include "datadog/logging.h"

namespace datadog::logging {

using datadog::core::DatadogCoreContext;
using datadog::core::DateTimeProvider;
using datadog::core::IDatadogCore;
using datadog::core::internal::Writer;

Logger::Logger(const LoggerConfiguration& options,
               const std::shared_ptr<IDatadogCore>& core)
    : configuration_(options), core_(core) {}

void Logger::Log(LogStatus log_status, const std::string_view& message) {
  auto core = core_.lock();
  auto time_provider = core->GetCoreContext().GetDateTimeProvider();
  auto timestamp = time_provider();

  LogEvent event{
      timestamp,
      log_status,
      std::string(message),
      std::nullopt,
      configuration_.service_name,
      configuration_.environment,
      configuration_.logger_name,
      configuration_.logger_version,
  };

  core->Write(LoggingFeature::feature_id, [event](auto context, auto writer) {
    std::stringstream ss;
    EncodeLogEvent(event, ss);

    writer->Write(ss);
  });
}

}  // namespace datadog::logging
