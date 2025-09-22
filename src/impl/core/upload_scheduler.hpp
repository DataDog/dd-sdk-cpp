// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>

#include "core/feature.hpp"
#include "core/types.hpp"
#include "platform/clock.hpp"
#include "platform/http.hpp"

namespace datadog::impl {

/**
 * Keeps track of when upload cycles should run next for all registered features.
 */
class UploadScheduler {
  /**
   * A record of the next scheduled upload cycle for the given feature.
   */
  struct Item {
    FeatureId feature_id;
    platform::Timestamp next_cycle_at;

    bool operator>(const Item& other) const {
      return next_cycle_at > other.next_cycle_at;
    }
  };

  /**
   * Used to read the current system time.
   */
  const platform::IClock& _clock;
  /**
   * Flag used to signal that scheduling is stopped and the upload thread should exit.
   */
  std::atomic<bool> _stopped{false};
  /**
   * Min-heap containing the timestamps at which the next upload cycle for each feature
   * should begin. Only accessed from the upload thread.
   */
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> _pq;
  /**
   * Wakes the upload thread in response to shutdown, so that the upload thread can
   * sleep for long periods of time without periodically waking up to check the shutdown
   * flag.
   */
  std::condition_variable _cv;
  /**
   * Synchronizes access to _cv. Does NOT synchronize access to _pq, as _pq is only
   * accessed on the upload thread.
   */
  std::mutex _mutex;

 public:
  explicit UploadScheduler(const platform::IClock& clock);

  /**
   * Ceases any further scheduling of upload cycles, setting an atomic flag that should
   * notify the upload thread to stop processing uploads.
   */
  void Stop();

  /**
   * Schedules the next upload cycle for the given feature to occur after the specified
   * delay.
   */
  void Schedule(FeatureId feature_id, platform::Duration next_cycle_in);

  /**
   * Blocks until the next feature is ready to be processed for upload, returning its ID
   * when the time comes to initiate an upload cycle for that feature.
   *
   * Returns std::nullopt to indicate that upload processing has stopped.
   */
  std::optional<FeatureId> WaitForNext();

 private:
  bool SleepFor(platform::Duration duration);
};

}  // namespace datadog::impl
