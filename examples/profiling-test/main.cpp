// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "datadog.hpp"
#include "datadog/profiling.hpp"
#include "dd-win-prof.h"

namespace {

const char* RequireEnvironmentVariable(const char* name) {
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') {
    std::fprintf(stderr, "Required environment variable %s is not set\n", name);
    std::exit(1);
  }
  return value;
}

void SpinFor(std::chrono::milliseconds duration) {
  const auto deadline = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < deadline) {
  }
}

}  // namespace

int main() {
  const char* client_token = RequireEnvironmentVariable("DD_CLIENT_TOKEN");
  const char* rum_application_id =
      RequireEnvironmentVariable("DD_RUM_APPLICATION_ID");

  datadog::CoreConfig core_config(client_token, "profiling-test", "dev");
  core_config.SetApplicationVersion("1.0.0");
  core_config.Internal_UseCustomEndpoint("https://browser-intake-datad0g.com");
  core_config.SetEventStorageLocation(".");
  core_config.SetInitialTrackingConsent(datadog::TrackingConsent::Granted);

  auto core = datadog::Core::Create(core_config);
  if (!core) {
    std::fprintf(stderr, "Failed to create the Datadog SDK Core\n");
    return 1;
  }

  ProfilerConfig profiler_config{};
  profiler_config.size = sizeof(ProfilerConfig);
  profiler_config.serviceName = "profiling-test";
  profiler_config.serviceVersion = "1.0.0";
  profiler_config.serviceEnvironment = "dev";
  profiler_config.uploadIntervalSeconds = 5;
  profiler_config.symbolizeCallstacks = true;

  auto profiling = datadog::Profiling::Register(core, &profiler_config);
  auto rum = datadog::Rum::Register(
      core, datadog::RumConfig(rum_application_id)
  );

  if (!core->Start()) {
    std::fprintf(stderr, "Failed to start the Datadog SDK\n");
    return 1;
  }

  struct View {
    const char* key;
    const char* name;
  };
  const View views[] = {
      {"view-1", "HomePage"},
      {"view-2", "SettingsPage"},
      {"view-3", "ProfilePage"},
  };

  for (const auto& view : views) {
    std::printf("Starting RUM view %s (%s)\n", view.name, view.key);
    rum->StartView(view.key, view.name);
    SpinFor(std::chrono::seconds(5));
    rum->StopView(view.key);
  }

  core->Stop();
  profiling.reset();
  rum.reset();
  core.reset();

  std::printf("Profiling and RUM smoke test completed\n");
  return 0;
}
