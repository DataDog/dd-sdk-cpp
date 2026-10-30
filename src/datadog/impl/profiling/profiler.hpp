// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <string>

#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Application identity passed to the profiler, used to tag the profiles it produces.
 */
struct ProfilerSettings {
  std::string service;
  std::string env;
  std::string version;
};

/**
 * Interface to a process-global sampling profiler.
 *
 * The production implementation wraps dd-win-prof; tests substitute a mock.
 */
class IProfiler {
 protected:
  IProfiler() = default;

 public:
  virtual ~IProfiler() = default;

  // An IProfiler is never copied or moved
  IProfiler(const IProfiler&) = delete;
  IProfiler& operator=(const IProfiler&) = delete;
  IProfiler(IProfiler&&) = delete;
  IProfiler& operator=(IProfiler&&) = delete;

  /**
   * Applies the given settings. Must be called before Start().
   */
  virtual bool Init(const ProfilerSettings& settings) = 0;

  /**
   * Starts profiling. Returns false if the profiler could not be started.
   */
  virtual bool Start() = 0;

  /**
   * Stops profiling. Safe to call if the profiler was never started.
   */
  virtual void Stop() = 0;
};

/**
 * Creates the IProfiler implementation for this build: one backed by dd-win-prof in
 * builds with profiling support (Windows x64, DD_ENABLE_PROFILING=ON).
 *
 * The profiler is process-global, so only one IProfiler may exist at a time: otherwise,
 * instances would overwrite each other's settings, restart a running profiler, or stop
 * profiling while another instance is still using it. Destroying the IProfiler allows a
 * later call to succeed.
 *
 * Returns null, after logging the reason, if this build has no profiler or if another
 * IProfiler already exists.
 */
std::unique_ptr<IProfiler> CreateProfiler(const DiagnosticLogger& logger);

}  // namespace datadog::impl
