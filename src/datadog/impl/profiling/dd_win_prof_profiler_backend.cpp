// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// IProfilerBackend implementation backed by dd-win-prof. Only compiled into builds with
// profiling support (see cmake/deps/dd-win-prof.cmake).

#include <dd-win-prof.h>
// The SDK-private part of dd-win-prof's interface, which hands profiles to the SDK
// instead of uploading them. It shares std types with dd-win-prof.dll, so it relies on
// the DLL being built with the SDK (see cmake/deps/dd-win-prof.cmake).
#include <internal/dd-win-rum-private.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <utility>

#include "datadog/impl/profiling/profiler_backend.hpp"

namespace datadog::impl {

namespace {

// Whether a DdWinProfBackend exists: dd-win-prof is process-global
std::atomic<bool> s_backend_exists{false};

// dd-win-prof reports times as milliseconds since the Unix epoch
Timestamp FromUnixMs(int64_t unix_ms) {
  return Timestamp{std::chrono::milliseconds(unix_ms)};
}

ProfileData ConvertProfilingData(ProfilingData&& data) {
  ProfileData profile;
  if (data.result != 0) {
    // Always non-null static text, valid until the DLL unloads
    profile.error = GetProfilingErrorMessage(data.result);
  } else {
    profile.pprof = std::move(data.pprof);
  }
  profile.start = FromUnixMs(data.startUnixTimeMs);
  profile.end = FromUnixMs(data.endUnixTimeMs);
  profile.is_last = data.isLast;
  return profile;
}

class DdWinProfBackend final : public IProfilerBackend {
 public:
  explicit DdWinProfBackend(const DiagnosticLogger& logger) : _logger(logger) {}

  ~DdWinProfBackend() override {
    // dd-win-prof only accepts a callback change while stopped, and delivers a last
    // profile when it stops, so stop it, then remove our callback, before this object
    // is considered gone. Stopping is safe if the profiler is not running.
    StopProfiler();
    if (!SetupProfilingDataCallback({})) {
      // dd-win-prof keeps calling the callback it has, which only holds what the
      // callback's owner gave it, so there is nothing more to do than report it
      _logger.Warning("Failed to remove the profiling callback from the profiler");
    }
    s_backend_exists.store(false);
  }

  bool SetProfileCallback(ProfileCallback callback) override {
    // An empty callback selects dd-win-prof's own upload, which is also what an empty
    // dd-win-prof callback means
    if (!callback) {
      return SetupProfilingDataCallback({});
    }
    // dd-win-prof copies the callback it is given
    return SetupProfilingDataCallback([callback =
                                           std::move(callback)](ProfilingData data) {
      callback(ConvertProfilingData(std::move(data)));
    });
  }

  bool Init(const ProfilerBackendSettings& settings) override {
    ProfilerConfig config = {};
    config.size = sizeof(ProfilerConfig);

    // Leave the endpoint, API key, and tuning parameters unset so that dd-win-prof
    // takes them from its environment variables
    config.noEnvVars = false;
    config.serviceName = settings.service.c_str();
    config.serviceEnvironment = settings.env.c_str();
    // Only pass a version if the application provided one
    config.serviceVersion =
        settings.version.empty() ? nullptr : settings.version.c_str();

    // dd-win-prof copies these strings, so they need only outlive this call
    return SetupProfiler(&config);
  }

  bool Start() override { return StartProfiler(); }

  void Stop() override { StopProfiler(); }

 private:
  // Used where there is no caller to report a failure to, i.e. in the destructor
  DiagnosticLogger _logger;
};

}  // namespace

std::unique_ptr<IProfilerBackend> CreateProfilerBackend(
    const DiagnosticLogger& logger
) {
  bool expected = false;
  if (!s_backend_exists.compare_exchange_strong(expected, true)) {
    logger.Warning(
        "Profiling is already enabled for another SDK instance in this process: the "
        "profiler is shared by the whole process, so only one instance can use it"
    );
    return nullptr;
  }
  return std::make_unique<DdWinProfBackend>(logger);
}

}  // namespace datadog::impl
