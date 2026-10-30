// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/profiling/profiler_backend.hpp"

using namespace datadog;

/**
 * Mock implementation of IProfilerBackend. Records the calls made to it; tests set the
 * results that it returns.
 */
class MockProfilerBackend : public impl::IProfilerBackend {
 public:
  bool init_result = true;
  bool start_result = true;
  int init_calls = 0;
  int start_calls = 0;
  int stop_calls = 0;
  impl::ProfilerBackendSettings last_settings;

  // IProfilerBackend interface
  bool Init(const impl::ProfilerBackendSettings& settings) override {
    init_calls++;
    last_settings = settings;
    return init_result;
  }

  bool Start() override {
    start_calls++;
    return start_result;
  }

  void Stop() override { stop_calls++; }
};
