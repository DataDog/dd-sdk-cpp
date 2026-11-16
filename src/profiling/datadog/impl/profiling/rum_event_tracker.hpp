// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_message.hpp"

namespace datadog::impl {

/** A RUM vital seen by the profiler. */
struct TrackedRumVital {
  UUID id;
  std::string name;
  std::optional<std::string> operation_key;
  Timestamp start;

  // True for an operation step, false for a vital that is complete on its own
  bool is_operation = false;

  // Absent for an operation that has started but not (yet) ended
  std::optional<Duration> duration;

  // Where the vital occurred. For an operation, this is where it started.
  RumEventContext context;
};

/** A RUM long task seen by the profiler. */
struct TrackedRumLongTask {
  UUID id;
  Timestamp start;
  Duration duration;
};

/** A RUM view that events seen by the profiler took place in. */
struct TrackedRumView {
  UUID id;
  std::string name;  // Empty if the view is unnamed
};

/**
 * The RUM events seen by the profiler since the previous snapshot, and the RUM context
 * in which the most recent of them was generated.
 */
struct RumEventSnapshot {
  // The application and session of the latest event, and the view it was in. Zero/empty
  // fields if no events were seen.
  RumEventContext context;
  std::vector<TrackedRumVital> vitals;
  std::vector<TrackedRumLongTask> long_tasks;

  // Every view that the events took place in, each once, in the order they were first
  // seen. A vital counts in the view it started in, and in the view of the step that
  // completed it.
  std::vector<TrackedRumView> views;

  bool IsEmpty() const { return vitals.empty() && long_tasks.empty(); }
};

/**
 * Records the RUM events that occur while the profiler runs, so that the metadata of
 * each profile can say which events it covers. This follows the dd-sdk-ios profiler:
 *
 * - Vitals that are not operation steps (e.g. app launch) are recorded as they arrive.
 * - An operation's start is recorded, and its end completes it by giving it a duration.
 *   The vital is identified by its name and operation key within the RUM session, and
 *   keeps the ID, start time and context of its start step. Update and retry steps are
 *   ignored, as is an end step with no matching start.
 * - Long tasks are recorded as they arrive.
 * - The views that these events took place in are collected, each once.
 * - An operation that has not ended is not tracked forever. See `TakeSnapshot()`.
 *
 * Each kind of event is capped at `kMaxEventsPerKind` between snapshots: when the cap
 * is reached, further events of that kind are dropped.
 *
 * Events arrive on the messaging thread, while snapshots are taken from whichever
 * thread the profiler exports on, so all state is guarded by a mutex.
 */
class RumEventTracker {
 public:
  static constexpr size_t kMaxEventsPerKind = 1000;

  // How long after its start an operation that has not ended is still expected to end;
  // matches the `cutOffTime` of dd-sdk-ios
  static constexpr Duration kOngoingOperationCutoff = std::chrono::seconds(60);

  void OnVital(const RumVitalGeneratedMessage& message);
  void OnLongTask(const RumLongTaskGeneratedMessage& message);

  /**
   * Returns everything recorded since the last call, and begins a new snapshot. `now`
   * is the time at which the profile that the snapshot describes ended.
   *
   * Operations that have started but not ended are included. They remain tracked, so
   * that a later end step can still complete them, and are reported again, with a
   * duration, by the snapshot that follows their end. While any remain, the snapshot's
   * context is that of the latest of them if nothing else has happened since, so that
   * each snapshot that reports them can say where they occurred.
   *
   * Operations are only kept while one of them started less than
   * `kOngoingOperationCutoff` before `now`. When none did, they are all assumed to have
   * been abandoned, and are reported this once and then forgotten, so that an operation
   * that never ends does not appear in every profile that follows.
   */
  RumEventSnapshot TakeSnapshot(const Timestamp& now);

  /** Forgets everything, including operations that have started but not ended. */
  void Reset();

 private:
  /**
   * Adds the view that `context` is in to `_views`, unless it is in no view, is already
   * there, or there is no room. Caller must hold `_mutex`.
   */
  void NoteView(const RumEventContext& context);

  std::mutex _mutex;
  RumEventContext _context;
  std::vector<TrackedRumVital> _vitals;
  std::vector<TrackedRumLongTask> _long_tasks;
  std::vector<TrackedRumView> _views;
};

}  // namespace datadog::impl
