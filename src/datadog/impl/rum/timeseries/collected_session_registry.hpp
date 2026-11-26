// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <cstddef>
#include <deque>
#include <mutex>

#include "datadog/uuid.hpp"

namespace datadog::impl {

/**
 * Tracks the sessions for which at least one timeseries data point was collected,
 * shared by all pipelines. Keeps only the MAX_SESSIONS most recently used sessions:
 * older ones produce no RUM events anymore. Thread-safe.
 */
class CollectedTimeseriesSessionRegistry final {
 public:
  static constexpr size_t MAX_SESSIONS = 3;

  void SetTimeseriesCollected(const UUID& session_id) {
    std::lock_guard lock(_mutex);
    const auto it = std::find(_sessions.begin(), _sessions.end(), session_id);
    if (it != _sessions.end()) {
      _sessions.erase(it);
    } else if (_sessions.size() >= MAX_SESSIONS) {
      _sessions.pop_front();
    }
    _sessions.push_back(session_id);
  }

  bool IsTimeseriesCollected(const UUID& session_id) const {
    std::lock_guard lock(_mutex);
    return std::find(_sessions.begin(), _sessions.end(), session_id) != _sessions.end();
  }

 private:
  mutable std::mutex _mutex;
  std::deque<UUID> _sessions;  // least recently used first
};

}  // namespace datadog::impl
