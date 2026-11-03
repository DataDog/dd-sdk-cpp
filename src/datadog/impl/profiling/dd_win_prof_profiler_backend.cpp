// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// IProfilerBackend implementation backed by dd-win-prof. Only compiled into builds with
// profiling support (see cmake/deps/dd-win-prof.cmake).

#include <dd-win-prof.h>

#include <atomic>
#include <memory>

#include "datadog/impl/profiling/profiler_backend.hpp"

namespace datadog::impl {

namespace {

// Whether a DdWinProfBackend exists: dd-win-prof is process-global
std::atomic<bool> s_backend_exists{false};

class DdWinProfBackend final : public IProfilerBackend {
 public:
  ~DdWinProfBackend() override { s_backend_exists.store(false); }

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
  return std::make_unique<DdWinProfBackend>();
}

}  // namespace datadog::impl
