// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "datadog/timestamp.hpp"

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
 * A finished profile delivered by the profiler, or the error that prevented one.
 */
struct ProfileData {
  /**
   * Why the profiler could not produce a profile. Empty if it succeeded, in which case
   * `pprof` holds the profile.
   */
  std::string error;

  /**
   * The serialized profile, in the compressed pprof form that the profiling intake
   * accepts. Empty if `error` is set.
   */
  std::vector<uint8_t> pprof;

  /** The period of time that the profile covers. */
  Timestamp start;
  Timestamp end;

  /** True for the last profile of a run: the one delivered when the profiler stops. */
  bool is_last = false;
};

/**
 * Receives each profile that the profiler produces. See
 * IProfilerBackend::SetProfileCallback().
 */
using ProfileCallback = std::function<void(ProfileData)>;

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
   * Sets the function that receives each profile. Calling this with an empty function
   * removes it, returning the profiler to its own default reporting mechanism.
   * Returns false if the profiler could not be given the callback.
   *
   * Must be called while the profiler is stopped, and before the first Init(). The
   * callback stays in place across Stop() and Start(), until it is replaced, removed,
   * or the backend is destroyed.
   *
   * The callback runs on a thread owned by the profiler, or, for the last profile of a
   * run (`ProfileData::is_last`), on the thread that calls Stop(). It must not block,
   * throw, or call back into this interface. A profile that the callback fails to
   * handle is not delivered again.
   */
  virtual bool SetProfileCallback(ProfileCallback callback) = 0;

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
