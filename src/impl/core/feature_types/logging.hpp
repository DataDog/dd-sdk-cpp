// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/logging.hpp"
#include "events/enum.hpp"
#include "events/omissible.hpp"
#include "events/struct.hpp"
#include "events/timestamp.hpp"

namespace datadog::impl {

DATADOG_STRING_ENUM(
    StringLogLevel,
    LogLevel,
    DATADOG_ENUM_VALUE(LogLevel::Debug, "debug"),
    DATADOG_ENUM_VALUE(LogLevel::Info, "info"),
    DATADOG_ENUM_VALUE(LogLevel::Notice, "notice"),
    DATADOG_ENUM_VALUE(LogLevel::Warn, "warn"),
    DATADOG_ENUM_VALUE(LogLevel::Error, "error"),
    DATADOG_ENUM_VALUE(LogLevel::Critical, "critical")
)

/**
 * JSON payload sent to intake in response to a single message emitted from a logger.
 */
struct LogEvent {
  StringLogLevel status;
  std::string service;
  IsoTimestamp date;
  std::string message;

  Omissible<std::string> logger_name;
  std::string logger_version;

  Omissible<UUID> rum_application_id;
  Omissible<UUID> rum_session_id;
  Omissible<UUID> rum_view_id;
  Omissible<UUID> rum_action_id;

  explicit LogEvent(
      std::string_view in_service_name,
      std::string_view in_logger_name,
      std::string_view in_logger_version
  )
      : status(LogLevel::Debug),
        service(in_service_name),
        date(Timestamp{}),
        message(""),
        logger_name(in_logger_name),
        logger_version(in_logger_version) {
    message.reserve(256);
  }

  void Reset() {
    rum_application_id = UUID::Zero;
    rum_session_id = UUID::Zero;
    rum_view_id = UUID::Zero;
    rum_action_id = UUID::Zero;
  }
};

DATADOG_JSON_STRUCT(
    LogEvent,

    DATADOG_JSON_FIELD(status),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(message),

    DATADOG_JSON_FIELD_NAME(logger_name, "logger.name"),
    DATADOG_JSON_FIELD_NAME(logger_version, "logger.version"),

    DATADOG_JSON_FIELD_NAME(rum_application_id, "application_id"),
    DATADOG_JSON_FIELD_NAME(rum_session_id, "session_id"),
    DATADOG_JSON_FIELD_NAME(rum_view_id, "view.id"),
    DATADOG_JSON_FIELD_NAME(rum_action_id, "user_action.id")

    // All user-specified attribute values are merged into the JSON object at top-level
    // and appended after these fields, with the exception of any property values whose
    // names are rejected by LogEvent_CanMergeUserAttribute().
)

constexpr bool LogEvent_CanMergeUserAttribute(std::string_view name) {
  // clang-format off
  if (name == "status") { return false; }
  if (name == "service") { return false; }
  if (name == "date") { return false; }
  if (name == "message") { return false; }
  if (name == "logger.name") { return false; }
  if (name == "logger.version") { return false; }
  if (name == "application_id") { return false; }
  if (name == "session_id") { return false; }
  if (name == "view.id") { return false; }
  if (name == "user_action.id") { return false; }
  // Reserved for future use:
  if (name == "_dd") { return false; }
  if (name == "usr") { return false; }
  if (name == "account") { return false; }
  if (name == "network") { return false; }
  if (name == "error") { return false; }
  if (name == "build_id") { return false; }
  if (name == "ddtags") { return false; }
  // clang-format on
  return true;
}

}  // namespace datadog::impl
