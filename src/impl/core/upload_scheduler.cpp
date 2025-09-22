// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/upload_scheduler.hpp"

namespace datadog::impl {

UploadScheduler::UploadScheduler(const platform::IClock& clock) : _clock(clock) {}

void UploadScheduler::Stop() {
  // Set the atomic shutdown flag so that if any thread is blocked in SleepUntil, it
  // will see on the next wakeup that it's time to exit
  _stopped.store(true, std::memory_order_release);

  // Signal the condition_variable to trigger a wakeup of any waiting threads
  {
    std::lock_guard lock(_mutex);
    _cv.notify_all();
  }
}

void UploadScheduler::Schedule(FeatureId feature_id, platform::Duration next_cycle_in) {
  // Add an entry to the priority queue, maintaining earliest-first ordering.
  // NOTE: Calls to Schedule() and WaitForNext() do not happen concurrently: the
  // upload thread uses this queue in a single-threaded fashion. Therefore, we don't
  // need to synchronize those operations or worry about waking consumers in response
  // to this push call.
  platform::Timestamp next_cycle_at = _clock.Now() + next_cycle_in;
  _pq.push({feature_id, next_cycle_at});
}

std::optional<FeatureId> UploadScheduler::WaitForNext() {
  // We shouldn't ordinarily encounter an empty queue, as usage of the priority queue
  // is single-threaded within the upload thread, and we always replace an item after
  // processing it
  if (_pq.empty()) {
    return std::nullopt;
  }

  // Peek the top-priority element, i.e. the scheduled upload with the lowest
  // next_cycle_at timestamp
  const auto& item = _pq.top();

  // If the next feature isn't ready yet, sleep until it is
  const platform::Timestamp now = _clock.Now();
  if (item.next_cycle_at > now) {
    // If we're awoken because of a shutdown, return nullopt to halt further
    // processing and signal that the upload thread should exit
    const platform::Duration sleep_duration = item.next_cycle_at - now;
    const bool is_shutting_down = SleepFor(sleep_duration);
    if (is_shutting_down) {
      return std::nullopt;
    }
  }

  // It's time to start a new upload cycle for this feature: return its ID to the
  // caller, popping it off the queue to be replaced by the caller.
  //
  // NOTE: Once again, all calls to WaitForNext() and Schedule() happen in the same
  // thread, so we don't need synchronization here: it's guaranteed that _pq is in the
  // same state here as it was before the SleepUntil() call.
  FeatureId feature_id = item.feature_id;
  _pq.pop();
  return feature_id;
}

bool UploadScheduler::SleepFor(platform::Duration duration) {
  // Wait for the desired delay, -or- until the atomic shutdown flag is set
  std::unique_lock lock(_mutex);
  return _cv.wait_for(lock, duration, [&] {
    // On spurious wakeup, go back to sleep unless shutdown flag is set
    return _stopped.load(std::memory_order_acquire);
  });
}

}  // namespace datadog::impl
