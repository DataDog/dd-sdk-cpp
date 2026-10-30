// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profile_metadata.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/types/json.hpp"
#include "datadog/impl/types/profiling.hpp"

namespace datadog::impl {

namespace {

// Placeholders until the profiling team confirms the values for this platform
constexpr std::string_view kFamily = "windows";
constexpr std::string_view kRuntime = "windows";

// Version of the event format. The only one that the backend supports, as on iOS and
// Android.
constexpr int64_t kEventVersion = 4;

constexpr std::string_view kLanguage = "cpp";
constexpr std::string_view kFormat = "pprof";
constexpr std::string_view kRemoteSymbols = "yes";

void AppendTag(std::string& tags, std::string_view key, std::string_view value) {
  // Tags without a value are left out
  if (value.empty()) {
    return;
  }
  if (!tags.empty()) {
    tags += ',';
  }
  tags += key;
  tags += ':';
  tags += value;
}

std::string BuildTags(const CoreContext& context, ProfilingOperation operation) {
  const std::string_view os_version = context.os != nullptr
                                          ? std::string_view(context.os->version)
                                          : std::string_view();

  // The Core has already built the unified service tags (service, version, env,
  // sdk_version, variant) once for all of its features, with any ',' or ':' stripped
  // from the application-provided values so that they cannot alter the tag list. Every
  // value added after them comes from the SDK or the operating system, not from the
  // application, so needs no sanitizing.
  std::string tags(context.per_event_ddtags);
  AppendTag(tags, "profiler_version", context.sdk_version);
  AppendTag(tags, "runtime_version", os_version);
  AppendTag(tags, "source", context.source);
  AppendTag(tags, "language", kLanguage);
  AppendTag(tags, "format", kFormat);
  AppendTag(tags, "remote_symbols", kRemoteSymbols);
  AppendTag(tags, "operation", StringProfilingOperation(operation).Name());
  return tags;
}

/** Describes `vital` for rum-mobile-events.json. */
ProfileRumEvent MakeRumEvent(const TrackedRumVital& vital) {
  ProfileRumEvent event(vital.id, ProfileRumEventType::Vital, vital.start);
  event.name = std::string_view(vital.name);
  if (vital.duration) {
    event.duration_ns = static_cast<int64_t>(vital.duration->count());
  }
  return event;
}

/** Describes `task` for rum-mobile-events.json. */
ProfileRumEvent MakeRumEvent(const TrackedRumLongTask& task) {
  ProfileRumEvent event(task.id, ProfileRumEventType::LongTask, task.start);
  event.duration_ns = static_cast<int64_t>(task.duration.count());
  return event;
}

}  // namespace

ProfileTags ProfileTags::FromContext(const CoreContext& context) {
  ProfileTags tags;
  for (size_t i = 0; i < kNumOperations; i++) {
    // i is less than kNumOperations, the number of ProfilingOperation values
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
    tags._tags[i] = BuildTags(context, static_cast<ProfilingOperation>(i));
  }
  return tags;
}

std::string_view ProfileTags::Get(ProfilingOperation operation) const {
  // ProfilingOperation values index its names, which fill _tags
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-constant-array-index)
  return _tags[static_cast<size_t>(operation)];
}

void BuildProfileEventJson(
    std::vector<uint8_t>& out,
    std::string_view tags_profiler,
    const ProfileTimeRange& range,
    const RumEventSnapshot& events
) {
  ProfileEvent event(
      range.start, range.end, tags_profiler, kFamily, kRuntime, kEventVersion
  );
  event.attachments = {kProfilePprofFilename, kProfileRumEventsFilename};

  // Correlate the profile with RUM. The event only views the names in `events`, which
  // outlives the encoding below.
  const RumEventContext& context = events.context;
  event.application_id = context.application_id;
  event.session_id = context.session_id;
  event.view_id.value.reserve(events.views.size());
  event.view_name.value.reserve(events.views.size());
  for (const TrackedRumView& view : events.views) {
    event.view_id.value.push_back(view.id);
    if (!view.name.empty()) {
      event.view_name.value.push_back(std::string_view(view.name));
    }
  }
  event.vital_id.value.reserve(events.vitals.size());
  event.vital_label.value.reserve(events.vitals.size());
  event.long_task_id.value.reserve(events.long_tasks.size());
  for (const TrackedRumVital& vital : events.vitals) {
    event.vital_id.value.push_back(vital.id);
    event.vital_label.value.push_back(std::string_view(vital.name));
  }
  for (const TrackedRumLongTask& task : events.long_tasks) {
    event.long_task_id.value.push_back(task.id);
  }

  EncodeJson(out, event);
}

void BuildRumEventsJson(std::vector<uint8_t>& out, const RumEventSnapshot& events) {
  std::vector<ProfileRumEvent> rum_events;
  rum_events.reserve(events.vitals.size() + events.long_tasks.size());
  for (const TrackedRumVital& vital : events.vitals) {
    rum_events.push_back(MakeRumEvent(vital));
  }
  for (const TrackedRumLongTask& task : events.long_tasks) {
    rum_events.push_back(MakeRumEvent(task));
  }
  EncodeJson(out, rum_events);
}

void ProfileMetadataSource::OnContextChanged(const CoreContext& context) {
  std::lock_guard<std::mutex> lock(_mutex);
  if (!_tags) {
    _tags.emplace(ProfileTags::FromContext(context));
  }
}

ProfileMetadata ProfileMetadataSource::BuildMetadata(const ProfileTimeRange& range) {
  // The tags are set once and never change, so they can be read after the lock is
  // released
  const ProfileTags* tags = nullptr;
  {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_tags) {
      tags = &*_tags;
    }
  }

  // A profile is only recorded after a context has arrived, so the tags are always set;
  // if not, the event goes without them
  const std::string_view tags_profiler =
      tags != nullptr ? tags->Get(ProfilingOperation::Continuous) : std::string_view();

  const RumEventSnapshot events = _tracker.TakeSnapshot(range.end);
  ProfileMetadata metadata;
  BuildProfileEventJson(metadata.event_json, tags_profiler, range, events);
  BuildRumEventsJson(metadata.rum_events_json, events);
  return metadata;
}

}  // namespace datadog::impl
