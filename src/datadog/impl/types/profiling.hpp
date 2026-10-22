// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <ratio>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/types/events/enum.hpp"
#include "datadog/impl/types/events/omissible.hpp"
#include "datadog/impl/types/events/struct.hpp"
#include "datadog/impl/types/events/timestamp.hpp"

namespace datadog::impl {

/** Why a profile was recorded; sent to the intake as the `operation` tag. */
enum class ProfilingOperation : uint8_t {
  ApplicationLaunch,
  Continuous,
  Custom,
};
DATADOG_STRING_ENUM(
    StringProfilingOperation,
    ProfilingOperation,
    DATADOG_ENUM_VALUE(ProfilingOperation::ApplicationLaunch, "application_launch"),
    DATADOG_ENUM_VALUE(ProfilingOperation::Continuous, "continuous"),
    DATADOG_ENUM_VALUE(ProfilingOperation::Custom, "custom")
)

/**
 * The `event.json` part of a profile upload: the description of a profile that the
 * profiling intake needs in order to ingest it. Follows the dd-sdk-ios profile event.
 *
 * Beyond the fixed description, it carries properties that correlate the profile with
 * the RUM events that took place while it was recorded.
 */
struct ProfileEvent {
  IsoTimestamp start;
  IsoTimestamp end;

  // Names of the other parts of the upload, e.g. `profile.pprof`. These values are
  // views: they must outlive the encoding of the event.
  std::vector<std::string_view> attachments;

  // Comma-separated `key:value` tags. Like the other fixed values below, this is a view
  // of a string that outlives the event, since it is the same for every profile.
  std::string_view tags_profiler;
  std::string_view family;
  std::string_view runtime;
  std::string_view version;

  // RUM context of the latest event seen while recording the profile. `view_name` is a
  // view, which must outlive the encoding of the event.
  OmitIfZero<UUID> application_id;
  OmitIfZero<UUID> session_id;
  OmitIfZero<UUID> view_id;
  OmitIfEmpty<std::string_view> view_name;
  OmitIfZero<UUID> action_id;

  // RUM vitals and long tasks seen while recording the profile; `vital_id` and
  // `vital_label` have one entry per vital, in the same order. The labels are views,
  // which must outlive the encoding of the event.
  OmitIfEmpty<std::vector<UUID>> vital_id;
  OmitIfEmpty<std::vector<std::string_view>> vital_label;
  OmitIfEmpty<std::vector<UUID>> long_task_id;

  ProfileEvent(
      Timestamp in_start,
      Timestamp in_end,
      std::string_view in_tags_profiler,
      std::string_view in_family,
      std::string_view in_runtime,
      std::string_view in_version
  )
      : start(in_start),
        end(in_end),
        tags_profiler(in_tags_profiler),
        family(in_family),
        runtime(in_runtime),
        version(in_version) {}
};

DATADOG_JSON_STRUCT(
    ProfileEvent,
    DATADOG_JSON_FIELD(start),
    DATADOG_JSON_FIELD(end),
    DATADOG_JSON_FIELD(attachments),
    DATADOG_JSON_FIELD(tags_profiler),
    DATADOG_JSON_FIELD(family),
    DATADOG_JSON_FIELD(runtime),
    DATADOG_JSON_FIELD(version),
    DATADOG_JSON_FIELD_NAME(application_id, "application.id"),
    DATADOG_JSON_FIELD_NAME(session_id, "session.id"),
    DATADOG_JSON_FIELD_NAME(view_id, "view.id"),
    DATADOG_JSON_FIELD_NAME(view_name, "view.name"),
    DATADOG_JSON_FIELD_NAME(action_id, "user_action.id"),
    DATADOG_JSON_FIELD_NAME(vital_id, "vital.id"),
    DATADOG_JSON_FIELD_NAME(vital_label, "vital.label"),
    DATADOG_JSON_FIELD_NAME(long_task_id, "long_task.id")
)

enum class ProfileRumEventType : uint8_t { Vital, LongTask };
DATADOG_STRING_ENUM(
    StringProfileRumEventType,
    ProfileRumEventType,
    DATADOG_ENUM_VALUE(ProfileRumEventType::Vital, "vital"),
    DATADOG_ENUM_VALUE(ProfileRumEventType::LongTask, "long_task")
)

/**
 * One element of the array in the `rum-mobile-events.json` part of a profile upload,
 * describing a RUM event that took place while the profile was recorded.
 */
struct ProfileRumEvent {
  UUID id;
  StringProfileRumEventType type;
  OmitIfEmpty<std::string_view> name;  // Vitals only; a view, must outlive encoding
  IntegerTimestamp<std::nano> start_ns;
  OmitIfNoValue<int64_t> duration_ns;  // Absent for an operation that has not ended

  ProfileRumEvent(const UUID& in_id, ProfileRumEventType in_type, Timestamp in_start)
      : id(in_id), type(in_type), start_ns(in_start) {}
};

DATADOG_JSON_STRUCT(
    ProfileRumEvent,
    DATADOG_JSON_FIELD(id),
    DATADOG_JSON_FIELD(type),
    DATADOG_JSON_FIELD(name),
    DATADOG_JSON_FIELD(start_ns),
    DATADOG_JSON_FIELD(duration_ns)
)

}  // namespace datadog::impl
