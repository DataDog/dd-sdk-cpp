// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// IProfiler implementation backed by dd-win-prof. Only compiled into builds with
// profiling support (see cmake/profiler.cmake).

#include <dd-win-prof.h>

#include <memory>

#include "datadog/impl/profiling/profiler.hpp"

namespace datadog::impl {

namespace {

class DdWinProfProfiler final : public IProfiler {
 public:
  bool Setup(const ProfilerSettings& settings) override {
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

std::unique_ptr<IProfiler> CreateProfiler() {
  return std::make_unique<DdWinProfProfiler>();
}

}  // namespace datadog::impl
