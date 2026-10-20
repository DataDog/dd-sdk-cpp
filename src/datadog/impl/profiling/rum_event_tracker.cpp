// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/rum_event_tracker.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace datadog::impl {

namespace {

bool IsSameOperation(
    const TrackedRumVital& vital, const RumVitalGeneratedMessage& message
) {
  return vital.is_operation && vital.name == message.name &&
         vital.operation_key == message.operation_key;
}

}  // namespace

void RumEventTracker::OnVital(const RumVitalGeneratedMessage& message) {
  std::lock_guard<std::mutex> lock(_mutex);

  // Only start and end steps matter to the profiler: updates and retries do not change
  // when the operation began or how long it took
  const bool is_operation = message.step_type.has_value();
  if (is_operation && *message.step_type != RumVitalStepType::Start &&
      *message.step_type != RumVitalStepType::End) {
    return;
  }

  // Only an operation can match a vital we already hold
  TrackedRumVital* existing = nullptr;
  if (is_operation) {
    const auto it =
        std::find_if(_vitals.begin(), _vitals.end(), [&](const TrackedRumVital& vital) {
          return IsSameOperation(vital, message);
        });
    if (it != _vitals.end()) {
      existing = &*it;
    }
  }

  if (is_operation && *message.step_type == RumVitalStepType::End) {
    // An end completes an operation that we saw start; otherwise there is nothing to
    // complete, and the end is dropped
    if (existing != nullptr) {
      existing->duration = message.timestamp - existing->start;
      _context = message.context;
    }
    return;
  }

  // A repeated start replaces the operation; anything else adds a vital, if there is
  // room for it
  if (existing == nullptr && _vitals.size() >= kMaxEventsPerKind) {
    return;
  }
  TrackedRumVital vital{
      message.vital_id,
      message.name,
      message.operation_key,
      message.timestamp,
      is_operation,
      message.duration
  };
  if (existing != nullptr) {
    *existing = std::move(vital);
  } else {
    _vitals.push_back(std::move(vital));
  }
  _context = message.context;
}

void RumEventTracker::OnLongTask(const RumLongTaskGeneratedMessage& message) {
  std::lock_guard<std::mutex> lock(_mutex);
  if (_long_tasks.size() >= kMaxEventsPerKind) {
    return;
  }
  _long_tasks.push_back({message.long_task_id, message.start, message.duration});
  _context = message.context;
}

RumEventSnapshot RumEventTracker::TakeSnapshot() {
  std::lock_guard<std::mutex> lock(_mutex);

  RumEventSnapshot snapshot;
  snapshot.context = std::exchange(_context, RumEventContext{});
  snapshot.long_tasks = std::exchange(_long_tasks, {});

  // Hand over every vital in order, moving those that are complete, and copying (and
  // keeping, compacted to the front) the operations still waiting for their end step
  snapshot.vitals.reserve(_vitals.size());
  size_t num_kept = 0;
  for (TrackedRumVital& vital : _vitals) {
    const bool is_pending = vital.is_operation && !vital.duration.has_value();
    if (!is_pending) {
      snapshot.vitals.push_back(std::move(vital));
      continue;
    }
    snapshot.vitals.push_back(vital);
    if (&_vitals[num_kept] != &vital) {
      _vitals[num_kept] = std::move(vital);
    }
    num_kept++;
  }
  _vitals.erase(_vitals.begin() + static_cast<std::ptrdiff_t>(num_kept), _vitals.end());
  return snapshot;
}

void RumEventTracker::Reset() {
  std::lock_guard<std::mutex> lock(_mutex);
  _context = RumEventContext{};
  _vitals.clear();
  _long_tasks.clear();
}

}  // namespace datadog::impl
