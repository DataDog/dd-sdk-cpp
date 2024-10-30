// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2023-Present
// Datadog, Inc.
#include "datadog/logger.h"

#include <sstream>
// cpp20: Replace with std::format.
#include <date/date.h>

#include "datadog/core_message.h"
#include "datadog/internal/sdk_version.h"

namespace datadog::logging {

using datadog::core::CoreMessage;
using datadog::core::Nanoseconds;
using namespace std::literals::string_view_literals;

// LogEvents are what get written to Datadog intake
struct LogEvent {
  Nanoseconds date;
  LogLevel status;
  std::string message;
  std::string_view application_version;
  std::string_view service_name;
  std::string_view logger_name;
  std::string_view logger_version;
};

constexpr std::string_view LogLevelAsString(LogLevel level) {
  switch (level) {
    case LogLevel::Debug:
      return "debug"sv;
    case LogLevel::Info:
      return "info"sv;
    case LogLevel::Notice:
      return "notice"sv;
    case LogLevel::Warn:
      return "warn"sv;
    case LogLevel::Error:
      return "error"sv;
    case LogLevel::Critical:
      return "critical"sv;
    default:
      return "unknown"sv;
  }
}

std::string EncodeTimestamp(Nanoseconds ns) {
  auto epoch =
      std::chrono::time_point<std::chrono::system_clock, Nanoseconds>();
  auto time = epoch + ns;
  // cpp20: Replace with std::format.
  return date::format("%FT%TZ", date::floor<std::chrono::milliseconds>(time));
}

void EncodeLogEvent(std::stringstream& ss, const LogEvent& log_event) {
  // TODO(jeff.ward): Replace with real JSON serialization
  ss << "{";
  ss << R"("date":")" << EncodeTimestamp(log_event.date) << R"(",)";
  ss << R"("status":")" << LogLevelAsString(log_event.status) << R"(",)";
  ss << R"("service":")" << log_event.service_name << R"(",)";
  if (!log_event.application_version.empty()) {
    ss << R"("version":")" << log_event.application_version << R"(",)";
  }
  ss << R"("logger": {)";
  {
    ss << R"("name":")" << log_event.logger_name << R"(",)";
    ss << R"("version":")" << log_event.logger_version << R"(")";
  }
  ss << "},";
  ss << R"("message":")" << log_event.message << R"(")";
  ss << "}";
}

// Cannot use `value_or` on std::optional<string> as it won't return the string
// by reference to create the string_view. This allows us to make a string_view
// reference to the parent string if a value exists, or use a default
// string_view instead.
std::string_view AsStringViewOr(const std::optional<std::string>& optional_str,
                                std::string_view or_value) {
  return optional_str.has_value() ? optional_str.value() : or_value;
}

void DatadogLogger::Log(LogLevel level, std::string_view message) {
  auto feature = feature_.lock();
  if (!feature) return;

  auto core = feature->GetCore();
  if (!core) return;

  auto timestamp = core->GetNow();
  // REVISIT: Get Core context or keep on the feature?
  const auto& context = core->GetContext();

  LogEvent event{
      timestamp,
      level,
      std::string{message},
      context.application_version,
      AsStringViewOr(configuration_.service, context.service),
      AsStringViewOr(configuration_.name, context.service),
      core::internal::kSdkVersion,
  };

  // TODO(jeff.ward): For now, immediately serialize the log event. We may want
  // to revisit this as there's a trade-off between the cpu and memory
  // performance of serializing here, vs capturing / copying the necessary data
  // and serializing on the Core thread.
  std::stringstream ss;
  EncodeLogEvent(ss, event);

  auto str_event = ss.str();
  CoreMessage core_message{{}, std::move(str_event)};

  core->SendMessage(DatadogLogging::feature_id, std::move(core_message));
}

}  // namespace datadog::logging
