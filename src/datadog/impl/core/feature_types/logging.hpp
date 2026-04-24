// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/attribute.hpp"
#include "datadog/logging.hpp"

#include "datadog/impl/core/events/enum.hpp"
#include "datadog/impl/core/events/omissible.hpp"
#include "datadog/impl/core/events/struct.hpp"
#include "datadog/impl/core/events/timestamp.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"

namespace datadog::impl {

/**
 * User info fields included in the `usr` object of a log event. Extra attributes are
 * merged inline alongside the standard string fields.
 */
struct LogUserInfo {
  OmitIfNoValue<std::string> id;
  OmitIfNoValue<std::string> name;
  OmitIfNoValue<std::string> email;
  Attribute extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    LogUserInfo,
    extra,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(email)
)

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

  OmitIfEmpty<std::string> logger_name;
  std::string logger_version;

  OmitIfZero<UUID> rum_application_id;
  OmitIfZero<UUID> rum_session_id;
  OmitIfZero<UUID> rum_view_id;
  OmitIfZero<UUID> rum_action_id;

  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;

  OmitIfNoValue<LogUserInfo> usr;

  // Custom user attributes to be merged into the JSON payload at top-level, condensed
  // from the union of global attributes, logger attributes, and message attributes
  Attribute user_attributes;

  explicit LogEvent(
      std::string_view in_service_name,
      std::string_view in_logger_name,
      std::string_view in_logger_version,
      size_t initial_attribute_capacity
  )
      : status(LogLevel::Debug),
        service(in_service_name),
        date(Timestamp{}),
        message(""),
        logger_name(in_logger_name),
        logger_version(in_logger_version),
        user_attributes(Attribute::Object(initial_attribute_capacity)) {
    message.reserve(256);
  }

  void Reset() {
    rum_application_id = UUID::Zero;
    rum_session_id = UUID::Zero;
    rum_view_id = UUID::Zero;
    rum_action_id = UUID::Zero;
    os = std::nullopt;
    device = std::nullopt;
    usr = std::nullopt;
  }
};

DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    LogEvent,
    user_attributes,

    DATADOG_JSON_FIELD(status),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(message),

    DATADOG_JSON_FIELD_NAME(logger_name, "logger.name"),
    DATADOG_JSON_FIELD_NAME(logger_version, "logger.version"),

    DATADOG_JSON_FIELD_NAME(rum_application_id, "application_id"),
    DATADOG_JSON_FIELD_NAME(rum_session_id, "session_id"),
    DATADOG_JSON_FIELD_NAME(rum_view_id, "view.id"),
    DATADOG_JSON_FIELD_NAME(rum_action_id, "user_action.id"),

    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(usr),

    // Field names reserved for future use (any custom attributes with conflicting names
    // will be ignored)
    DATADOG_JSON_RESERVED_FIELD(_dd),
    DATADOG_JSON_RESERVED_FIELD(account),
    DATADOG_JSON_RESERVED_FIELD(network),
    DATADOG_JSON_RESERVED_FIELD(error),
    DATADOG_JSON_RESERVED_FIELD(build_id),
    DATADOG_JSON_RESERVED_FIELD(ddtags)
)

}  // namespace datadog::impl
