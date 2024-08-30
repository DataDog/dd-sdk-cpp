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
  auto time_provider = core->GetTimeProvider();
  auto timestamp = time_provider();

  auto capture_message = std::string(message);

  // Write gives us back a writer for the specified feature, taking into
  // account batching and preferred storage mechanism for the configured core.
  // It also hands us back the core context at the time of the write.
  //
  // Implementation of Write is still up in the air, but one possible
  // implementation it for Write to use a thread managed by the Core and not
  // block, so we capture variables used in the lambda by value.
  //
  // TODO: This multistage copy (message->captured_message->LogEvent::message)
  // is definately inefficient, and we would need a way to both capture
  // and avoid repeated copies of the message and other future parameters of
  // this funciton
  core->Write(LoggingFeature::feature_id, [=](auto context, auto writer) {
    std::stringstream ss;

    // LogEvent needs to be created inside the writer because it might need
    // to use information from context
    LogEvent event{
        timestamp,
        log_status,
        std::string(message),
        std::nullopt,
        context.GetApplicationVersion(),
        configuration_.service_name,
        configuration_.environment,
        configuration_.logger_name,
        configuration_.logger_version,
    };

    EncodeLogEvent(event, ss);

    writer->Write(ss);
  });
}

}  // namespace datadog::logging
