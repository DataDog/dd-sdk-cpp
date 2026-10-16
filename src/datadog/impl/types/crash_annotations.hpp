// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string_view>

#include "datadog/attribute.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/types/events/omissible.hpp"
#include "datadog/impl/types/events/struct.hpp"
#include "datadog/impl/types/json.hpp"

namespace datadog::impl {

/**
 * A value encoded in `dd.config`.
 */
struct ConfigAnnotation {
  std::string_view service;
  std::string_view env;
  std::string_view version;
  std::string_view variant;
  std::string_view source;
  std::string_view sdk_version;
};
DATADOG_JSON_STRUCT(
    ConfigAnnotation,
    DATADOG_JSON_FIELD(service),
    DATADOG_JSON_FIELD(env),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(variant),
    DATADOG_JSON_FIELD(source),
    DATADOG_JSON_FIELD(sdk_version)
)

/**
 * A value encoded in `dd.os`.
 */
struct OsAnnotation {
  std::string_view name;
  std::string_view version;
  std::string_view build;
  std::string_view version_major;
};
DATADOG_JSON_STRUCT(
    OsAnnotation,
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD(build),
    DATADOG_JSON_FIELD(version_major)
)

/**
 * A value encoded in `dd.device`.
 */
struct DeviceAnnotation {
  std::string_view type;
  std::string_view name;
  std::string_view model;
  std::string_view brand;
  std::string_view architecture;
  std::string_view locale;
  std::string_view time_zone;
};
DATADOG_JSON_STRUCT(
    DeviceAnnotation,
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(model),
    DATADOG_JSON_FIELD(brand),
    DATADOG_JSON_FIELD(architecture),
    DATADOG_JSON_FIELD(locale),
    DATADOG_JSON_FIELD(time_zone)
)

/**
 * A value encoded in `dd.usr`.
 */
struct UsrAnnotation {
  OmitIfEmpty<std::string_view> id;
  OmitIfEmpty<std::string_view> name;
  OmitIfEmpty<std::string_view> email;
  OmitIfZero<UUID> anonymous_id;
  const Attribute& extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    UsrAnnotation,
    extra,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(email),
    DATADOG_JSON_FIELD(anonymous_id)
)

/**
 * A value encoded in `dd.account`.
 */
struct AccountAnnotation {
  OmitIfEmpty<std::string_view> id;
  OmitIfEmpty<std::string_view> name;
  const Attribute& extra;
};
DATADOG_JSON_STRUCT_WITH_EXTRA_ATTRIBUTES(
    AccountAnnotation, extra, DATADOG_JSON_FIELD(id), DATADOG_JSON_FIELD(name)
)

/**
 * A value encoded in `dd.rum.config`.
 */
struct RumConfigAnnotation {
  OmitIfZero<UUID> application_id;
  float session_sample_rate;
};
DATADOG_JSON_STRUCT(
    RumConfigAnnotation,
    DATADOG_JSON_FIELD(application_id),
    DATADOG_JSON_FIELD(session_sample_rate)
)

/**
 * A value encoded in `dd.rum.session`.
 */
struct RumSessionAnnotation {
  OmitIfZero<UUID> id;
  bool is_sampled;
  bool is_active;
  bool is_initial;
  bool has_tracked_any_view;
  bool did_start_with_replay;
};
DATADOG_JSON_STRUCT(
    RumSessionAnnotation,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(is_sampled),
    DATADOG_JSON_FIELD(is_active),
    DATADOG_JSON_FIELD(is_initial),
    DATADOG_JSON_FIELD(has_tracked_any_view),
    DATADOG_JSON_FIELD(did_start_with_replay)
)

}  // namespace datadog::impl
