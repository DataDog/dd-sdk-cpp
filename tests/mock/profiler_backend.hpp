// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/timestamp.hpp"

#include "datadog/impl/profiling/profiler_backend.hpp"

using namespace datadog;

/**
 * A profile for a test to have MockProfilerBackend deliver. Unlike the ProfileView that
 * the profiler delivers, it owns its bytes, so a test can build one and hand it over.
 * On success, `error` is empty.
 */
struct MockProfile {
  std::vector<uint8_t> pprof;
  std::string error;
  Timestamp start;
  Timestamp end;
  bool is_last = false;
};

/**
 * Mock implementation of IProfilerBackend. Records the calls made to it; tests set the
 * results that it returns. Like the dd-win-prof backend, it drops its callback when it
 * stops.
 */
class MockProfilerBackend : public impl::IProfilerBackend {
 public:
  bool set_callback_result = true;
  bool init_result = true;
  bool start_result = true;
  impl::ProfilerBackendSettings last_settings;
  std::vector<std::string> calls;

  // Whether a callback was set when Init() was last called: the profiler must have one
  // before it is configured
  bool callback_set_at_init = false;

  // If set, Stop() delivers this profile to the callback, once, from inside Stop(), as
  // the real profiler delivers the last profile of a run as it stops
  std::optional<MockProfile> profile_on_stop;

  int CountCalls(std::string_view name) const {
    return static_cast<int>(std::count(calls.begin(), calls.end(), name));
  }

  /** True if a callback is currently set. */
  bool HasCallback() const { return static_cast<bool>(_callback); }

  /**
   * Delivers `profile` to the callback, from the calling thread, as the profiler would,
   * as a view of the bytes that `profile` owns. Returns false, delivering nothing, if
   * there is no callback.
   */
  bool DeliverProfile(const MockProfile& profile) const {
    if (!_callback) {
      return false;
    }
    impl::ProfileView view;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    view.pprof = {
        reinterpret_cast<const char*>(profile.pprof.data()), profile.pprof.size()
    };
    view.error = profile.error;
    view.start = profile.start;
    view.end = profile.end;
    view.is_last = profile.is_last;
    _callback(view);
    return true;
  }

  // IProfilerBackend interface
  bool SetProfileCallback(impl::ProfileCallback callback) override {
    calls.push_back(callback ? "register" : "unregister");
    if (!set_callback_result) {
      return false;
    }
    _callback = std::move(callback);
    return true;
  }

  bool Init(const impl::ProfilerBackendSettings& settings) override {
    calls.push_back("init");
    last_settings = settings;
    callback_set_at_init = HasCallback();
    return init_result;
  }

  bool Start() override {
    calls.push_back("start");
    return start_result;
  }

  void Stop() override {
    calls.push_back("stop");
    if (profile_on_stop) {
      MockProfile profile = std::move(*profile_on_stop);
      profile_on_stop.reset();
      profile.is_last = true;
      DeliverProfile(profile);
    }
    // As the real backend does, drop the callback once the final profile is delivered
    _callback = {};
  }

 private:
  impl::ProfileCallback _callback;
};
