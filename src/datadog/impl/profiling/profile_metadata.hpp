// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/profiling/rum_event_tracker.hpp"
#include "datadog/impl/types/profiling.hpp"

namespace datadog::impl {

/**
 * The period of time covered by a profile.
 */
struct ProfileTimeRange {
  Timestamp start;
  Timestamp end;
};

/**
 * The SDK-built metadata of a profile, to be uploaded with the profile itself. Each
 * member is the content of one part of the multipart request sent to the profiling
 * intake: `event_json` as `event.json`, and `rum_events_json` as the
 * `rum-mobile-events.json` attachment that `event_json` lists. Both are encoded JSON
 * text.
 */
struct ProfileMetadata {
  std::vector<uint8_t> event_json;
  std::vector<uint8_t> rum_events_json;
};

// Names of the parts of the multipart request sent to the profiling intake.
// `event.json` lists the other parts as its `attachments`.
inline constexpr std::string_view kProfileEventFilename = "event.json";
inline constexpr std::string_view kProfilePprofFilename = "profile.pprof";
inline constexpr std::string_view kProfileRumEventsFilename = "rum-mobile-events.json";

/**
 * The `tags_profiler` value of a profile event, for each profiling operation.
 *
 * Every tag but `operation` comes from the immutable part of the context, so the values
 * are built once, up front, rather than for each profile, much as `per_event_ddtags` is
 * built once for the context.
 */
class ProfileTags {
 public:
  /** Builds the tags for every operation from the given context's immutable values. */
  static ProfileTags FromContext(const CoreContext& context);

  /** Returns the tags for the given operation. Valid for the life of this object. */
  std::string_view Get(ProfilingOperation operation) const;

 private:
  static constexpr size_t kNumOperations = std::size(ProfilingOperationValues);
  std::array<std::string, kNumOperations> _tags;
};

/**
 * Encodes the content of `event.json` into `out`, replacing its contents: the
 * description of a profile that the intake needs in order to ingest it. Follows the
 * dd-sdk-ios profile event, with the family and runtime of this platform.
 *
 * Alongside the fixed description (time range, tags, attachments), it carries IDs that
 * correlate the profile with the RUM events in `events`: the IDs of the application and
 * session in which the latest event occurred, the IDs and names of the views that the
 * events took place in, and the IDs of the vitals and long tasks that were seen.
 */
void BuildProfileEventJson(
    std::vector<uint8_t>& out,
    std::string_view tags_profiler,
    const ProfileTimeRange& range,
    const RumEventSnapshot& events
);

/**
 * Encodes the content of the `rum-mobile-events.json` attachment into `out`, replacing
 * its contents: a JSON array describing each RUM vital and long task in `events`, with
 * its ID, start time, and duration if known.
 */
void BuildRumEventsJson(std::vector<uint8_t>& out, const RumEventSnapshot& events);

/**
 * Gathers what is needed to build the metadata of a profile: the tags that identify the
 * application, learned from the first context update, and the RUM events seen so far.
 *
 * Fed from the messaging thread and queried from whichever thread builds a profile's
 * metadata, so every member is thread-safe. It owns its own state and shares no lock
 * with the `Profiling` feature, so it is safe to query while the feature is starting or
 * stopping the profiler.
 */
class ProfileMetadataSource {
 public:
  /**
   * Builds the tags from the given context, if they have not been built yet. Only the
   * first context matters, since the values that the tags use never change.
   */
  void OnContextChanged(const CoreContext& context);

  void OnVital(const RumVitalGeneratedMessage& message) { _tracker.OnVital(message); }

  void OnLongTask(const RumLongTaskGeneratedMessage& message) {
    _tracker.OnLongTask(message);
  }

  /** Forgets the RUM events seen so far. */
  void ResetRumEvents() { _tracker.Reset(); }

  /**
   * Builds the metadata for a profile covering `range`, consuming the RUM events seen
   * since the previous call.
   */
  ProfileMetadata BuildMetadata(const ProfileTimeRange& range);

 private:
  std::mutex _mutex;

  // Empty until the first context arrives. Set once, under _mutex, and never modified
  // or reset after that, so a pointer to its contents stays valid, and safe to read
  // without the lock, for the life of this object.
  std::optional<ProfileTags> _tags;

  RumEventTracker _tracker;
};

}  // namespace datadog::impl
