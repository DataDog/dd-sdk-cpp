// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include "datadog/impl/profiling/profiler.hpp"

using namespace datadog;

/**
 * Mock implementation of IProfiler. Records the calls made to it; tests set the results
 * that it returns.
 */
class MockProfiler : public impl::IProfiler {
 public:
  bool setup_result = true;
  bool start_result = true;
  int setup_calls = 0;
  int start_calls = 0;
  int stop_calls = 0;
  impl::ProfilerSettings last_settings;

  // IProfiler interface
  bool Setup(const impl::ProfilerSettings& settings) override {
    setup_calls++;
    last_settings = settings;
    return setup_result;
  }

  bool Start() override {
    start_calls++;
    return start_result;
  }

  void Stop() override { stop_calls++; }
};
