// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/attribute.hpp"
#include "datadog/logging.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/types/events/enum.hpp"
#include "datadog/impl/types/events/omissible.hpp"
#include "datadog/impl/types/events/struct.hpp"
#include "datadog/impl/types/events/timestamp.hpp"

namespace datadog::impl {

/**
 * User info fields included in the `usr` object of a log event. Extra attributes are
 * merged inline alongside the standard string fields.
 */
struct LogUserInfo {
  OmitIfEmpty<std::string> id;
  OmitIfEmpty<std::string> name;
  OmitIfEmpty<std::string> email;
  OmitIfZero<UUID> anonymous_id;
  Attribute extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    LogUserInfo,
    extra,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(email),
    DATADOG_JSON_FIELD(anonymous_id)
)

/**
 * Account info fields included in the `account` object of a log event. Extra attributes
 * are merged inline alongside the standard string fields.
 */
struct LogAccountInfo {
  OmitIfEmpty<std::string> id;
  OmitIfEmpty<std::string> name;
  Attribute extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    LogAccountInfo, extra, DATADOG_JSON_FIELD(id), DATADOG_JSON_FIELD(name)
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

  OmitIfEmpty<std::string> error_message;
  OmitIfEmpty<std::string> error_kind;
  OmitIfEmpty<std::string> error_stack;
  OmitIfEmpty<std::string> error_source_type;
  OmitIfEmpty<std::string> error_fingerprint;

  std::string ddtags;

  OmitIfEmpty<std::string> logger_name;
  std::string logger_version;

  OmitIfZero<UUID> rum_application_id;
  OmitIfZero<UUID> rum_session_id;
  OmitIfZero<UUID> rum_view_id;
  OmitIfZero<UUID> rum_action_id;

  OmitIfNoValue<RumOSProperties> os;
  OmitIfNoValue<RumDeviceProperties> device;

  OmitIfNoValue<LogUserInfo> usr;
  OmitIfNoValue<LogAccountInfo> account;

  // Custom user attributes to be merged into the JSON payload at top-level, condensed
  // from the union of global attributes, logger attributes, and message attributes
  Attribute user_attributes;

  /**
   * Initializes a payload for a single log event.
   */
  explicit LogEvent(
      LogLevel in_status,
      std::string_view in_service,
      Timestamp in_date,
      std::string in_message,
      std::string_view in_ddtags,
      std::string_view in_logger_name,
      std::string_view in_logger_version
  )
      : status(in_status),
        service(in_service),
        date(in_date),
        message(std::move(in_message)),
        ddtags(in_ddtags),
        logger_name(in_logger_name),
        logger_version(in_logger_version) {}
};

DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    LogEvent,
    user_attributes,

    DATADOG_JSON_FIELD(status),
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(date),
    DATADOG_JSON_FIELD(message),
    DATADOG_JSON_FIELD_NAME(error_message, "error.message"),
    DATADOG_JSON_FIELD_NAME(error_kind, "error.kind"),
    DATADOG_JSON_FIELD_NAME(error_stack, "error.stack"),
    DATADOG_JSON_FIELD_NAME(error_source_type, "error.source_type"),
    DATADOG_JSON_FIELD_NAME(error_fingerprint, "error.fingerprint"),

    DATADOG_JSON_FIELD(ddtags),

    DATADOG_JSON_FIELD_NAME(logger_name, "logger.name"),
    DATADOG_JSON_FIELD_NAME(logger_version, "logger.version"),

    // These property names are styled inconsistently; this is intentional
    DATADOG_JSON_FIELD_NAME(rum_application_id, "application_id"),
    DATADOG_JSON_FIELD_NAME(rum_session_id, "session_id"),
    DATADOG_JSON_FIELD_NAME(rum_view_id, "view.id"),
    DATADOG_JSON_FIELD_NAME(rum_action_id, "user_action.id"),

    DATADOG_JSON_FIELD(os),
    DATADOG_JSON_FIELD(device),
    DATADOG_JSON_FIELD(usr),
    DATADOG_JSON_FIELD(account),

    // Field names reserved for future use (any custom attributes with conflicting names
    // will be ignored)
    DATADOG_JSON_RESERVED_FIELD(_dd),
    DATADOG_JSON_RESERVED_FIELD(network),
    DATADOG_JSON_RESERVED_FIELD(error),
    DATADOG_JSON_RESERVED_FIELD(build_id)
)

}  // namespace datadog::impl
