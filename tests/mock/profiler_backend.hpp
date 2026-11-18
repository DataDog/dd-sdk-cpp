// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/impl/profiling/profiler_backend.hpp"

using namespace datadog;

/**
 * Mock implementation of IProfilerBackend. Records the calls made to it; tests set the
 * results that it returns.
 */
class MockProfilerBackend : public impl::IProfilerBackend {
 public:
  bool set_callback_result = true;
  bool init_result = true;
  bool start_result = true;
  impl::ProfilerBackendSettings last_settings;
  std::vector<std::string> calls;

  int CountCalls(std::string_view name) const {
    return static_cast<int>(std::count(calls.begin(), calls.end(), name));
  }

  bool HasCallback() const { return static_cast<bool>(_callback); }

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
    if (!init_result) {
      _callback = {};
      calls.push_back("unregister");
    }
    return init_result;
  }

  bool Start() override {
    calls.push_back("start");
    if (!start_result) {
      _callback = {};
      calls.push_back("unregister");
    }
    return start_result;
  }

  void Stop() override {
    calls.push_back("stop");
    _callback = {};
    calls.push_back("unregister");
  }

 private:
  impl::ProfileCallback _callback;
};
