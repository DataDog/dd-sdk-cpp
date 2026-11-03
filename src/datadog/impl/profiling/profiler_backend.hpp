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
struct ProfilerBackendSettings {
  std::string service;
  std::string env;
  std::string version;
};

/**
 * Interface to a process-global sampling profiler.
 *
 * The production implementation wraps dd-win-prof; tests substitute a mock.
 */
class IProfilerBackend {
 protected:
  IProfilerBackend() = default;

 public:
  virtual ~IProfilerBackend() = default;

  // An IProfilerBackend is never copied or moved
  IProfilerBackend(const IProfilerBackend&) = delete;
  IProfilerBackend& operator=(const IProfilerBackend&) = delete;
  IProfilerBackend(IProfilerBackend&&) = delete;
  IProfilerBackend& operator=(IProfilerBackend&&) = delete;

  /**
   * Applies the given settings. Must be called before Start().
   */
  virtual bool Init(const ProfilerBackendSettings& settings) = 0;

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
 * Creates the IProfilerBackend implementation for this build: one backed by dd-win-prof
 * in builds with profiling support (Windows x64, DD_ENABLE_PROFILING=ON).
 *
 * The profiler is process-global, so only one IProfilerBackend may exist at a time:
 * otherwise, instances would overwrite each other's settings, restart a running
 * profiler, or stop profiling while another instance is still using it. Destroying the
 * IProfilerBackend allows a later call to succeed.
 *
 * Returns null, after logging the reason, if this build has no profiler or if another
 * IProfilerBackend already exists.
 */
std::unique_ptr<IProfilerBackend> CreateProfilerBackend(const DiagnosticLogger& logger);

}  // namespace datadog::impl
