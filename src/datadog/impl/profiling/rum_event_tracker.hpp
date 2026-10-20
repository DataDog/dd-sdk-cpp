// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

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
};

/** A RUM long task seen by the profiler. */
struct TrackedRumLongTask {
  UUID id;
  Timestamp start;
  Duration duration;
};

/**
 * The RUM events seen by the profiler since the previous snapshot, and the RUM context
 * in which the most recent of them was generated.
 */
struct RumEventSnapshot {
  // Zero/empty fields if no events were seen
  RumEventContext context;
  std::vector<TrackedRumVital> vitals;
  std::vector<TrackedRumLongTask> long_tasks;

  bool IsEmpty() const { return vitals.empty() && long_tasks.empty(); }
};

/**
 * Records the RUM events that occur while the profiler runs, so that the metadata of
 * each profile can say which events it covers. This follows the dd-sdk-ios profiler:
 *
 * - Vitals that are not operation steps (e.g. app launch) are recorded as they arrive.
 * - An operation's start is recorded, and its end completes it by giving it a duration.
 *   The vital is identified by its name and operation key, and keeps the ID and start
 *   time of its start step. Update and retry steps are ignored, as is an end step with
 *   no matching start.
 * - Long tasks are recorded as they arrive.
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

  void OnVital(const RumVitalGeneratedMessage& message);
  void OnLongTask(const RumLongTaskGeneratedMessage& message);

  /**
   * Returns everything recorded since the last call, and begins a new snapshot.
   *
   * Operations that have started but not ended are included, and remain tracked so that
   * a later end step can still complete them: they are reported again, with a duration,
   * by the snapshot that follows their end.
   */
  RumEventSnapshot TakeSnapshot();

  /** Forgets everything, including operations that have started but not ended. */
  void Reset();

 private:
  std::mutex _mutex;
  RumEventContext _context;
  std::vector<TrackedRumVital> _vitals;
  std::vector<TrackedRumLongTask> _long_tasks;
};

}  // namespace datadog::impl
