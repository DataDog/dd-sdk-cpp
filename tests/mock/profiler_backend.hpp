// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <utility>

#include "datadog/impl/profiling/profiler_backend.hpp"

using namespace datadog;

/**
 * Mock implementation of IProfilerBackend. Records the calls made to it; tests set the
 * results that it returns, and can deliver profiles to the callback it was given.
 */
class MockProfilerBackend : public impl::IProfilerBackend {
 public:
  bool set_callback_result = true;
  bool init_result = true;
  bool start_result = true;
  int set_callback_calls = 0;
  int init_calls = 0;
  int start_calls = 0;
  int stop_calls = 0;
  impl::ProfilerBackendSettings last_settings;

  // Whether a callback was set when Init() was last called: the profiler must have one
  // before it is configured
  bool callback_set_at_init = false;

  // If set, Stop() delivers this profile to the callback, once, from inside Stop(), as
  // the real profiler delivers the last profile of a run as it stops
  std::optional<impl::ProfileData> profile_on_stop;

  /**
   * Delivers `profile` to the callback, from the calling thread, as the profiler would.
   * Returns false, delivering nothing, if there is no callback.
   */
  bool DeliverProfile(impl::ProfileData profile) const {
    if (!_callback) {
      return false;
    }
    _callback(std::move(profile));
    return true;
  }

  /** True if a callback is currently set. */
  bool HasCallback() const { return static_cast<bool>(_callback); }

  // IProfilerBackend interface
  bool SetProfileCallback(impl::ProfileCallback callback) override {
    set_callback_calls++;
    if (!set_callback_result) {
      return false;
    }
    _callback = std::move(callback);
    return true;
  }

  bool Init(const impl::ProfilerBackendSettings& settings) override {
    init_calls++;
    last_settings = settings;
    callback_set_at_init = HasCallback();
    return init_result;
  }

  bool Start() override {
    start_calls++;
    return start_result;
  }

  void Stop() override {
    stop_calls++;
    if (profile_on_stop) {
      impl::ProfileData profile = std::move(*profile_on_stop);
      profile_on_stop.reset();
      profile.is_last = true;
      DeliverProfile(std::move(profile));
    }
  }

 private:
  impl::ProfileCallback _callback;
};
