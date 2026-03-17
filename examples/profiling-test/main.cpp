// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

// Minimal smoke test: Core + Profiling + RUM end-to-end.
// Spins CPU for ~15s across 3 RUM view transitions so the profiler
// collects wall-time / CPU samples tagged with RUM context.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "datadog.hpp"
#include "datadog/profiling.hpp"

// dd-win-prof.h uses uint32_t etc. — must come after <cstdint>
// clang-format off
#include <dd-win-prof.h>
// clang-format on

// ---------------------------------------------------------------------------
// Busy-loop for `duration_ms` milliseconds (no Sleep — we want CPU samples).
// ---------------------------------------------------------------------------
static void spin(int duration_ms) {
  auto start = std::chrono::steady_clock::now();
  while (true) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    if (elapsed >= duration_ms) break;
  }
}

// ---------------------------------------------------------------------------
static const char* require_env(const char* name) {
  const char* value = std::getenv(name);
  if (!value || value[0] == '\0') {
    std::fprintf(stderr, "ERROR: environment variable %s is not set\n", name);
    std::exit(1);
  }
  return value;
}

// ---------------------------------------------------------------------------
int main() {
  std::printf("=== profiling-test: Core + Profiling + RUM smoke test ===\n\n");

  // 1. Read required env vars (profiler reads DD_API_KEY, DD_SITE, etc. directly)
  const char* client_token = require_env("DD_CLIENT_TOKEN");
  const char* rum_app_id = require_env("DD_RUM_APPLICATION_ID");

  std::printf("[env] DD_CLIENT_TOKEN  = %.8s...\n", client_token);
  std::printf("[env] DD_RUM_APPLICATION_ID = %s\n\n", rum_app_id);

  // 2. Create Core
  datadog::CoreConfig core_config(client_token, "profiling-test", "dev");
  core_config.SetApplicationVersion("1.0.0");
  core_config.SetEventStorageLocation(".");
  core_config.SetInitialTrackingConsent(datadog::TrackingConsent::Granted);
  core_config.Internal_UseCustomEndpoint("https://datad0g.com");

  auto core = datadog::Core::Create(core_config);
  if (!core) {
    std::fprintf(stderr, "Failed to create Core\n");
    return 1;
  }
  std::printf("[core] created\n");

  // 3. Register Profiling (reads DD_API_KEY, DD_SITE, etc. from env)
  ProfilerConfig profiler_config = {};
  profiler_config.size = sizeof(ProfilerConfig);
  profiler_config.serviceName = "profiling-test";
  profiler_config.serviceVersion = "1.0.0";
  profiler_config.serviceEnvironment = "dev";
  profiler_config.symbolizeCallstacks = true;

  auto profiling = datadog::Profiling::Register(core, &profiler_config);
  if (!profiling) {
    std::fprintf(stderr, "Failed to register Profiling\n");
    return 1;
  }
  std::printf("[profiling] registered (agentless, symbolize=true)\n");

  // 4. Register RUM
  auto rum = datadog::Rum::Register(core, datadog::RumConfig(rum_app_id));
  if (!rum) {
    std::fprintf(stderr, "Failed to register RUM\n");
    return 1;
  }
  std::printf("[rum] registered (app_id=%s)\n\n", rum_app_id);

  // 5. Start
  if (!core->Start()) {
    std::fprintf(stderr, "Failed to start Core\n");
    return 1;
  }
  std::printf("[core] started — profiler + RUM wired\n\n");

  // 6. Simulate 3 view transitions with CPU work
  struct ViewStep {
    const char* key;
    const char* name;
    int spin_ms;
  };

  ViewStep views[] = {
      {"view-1", "HomePage", 5000},
      {"view-2", "SettingsPage", 5000},
      {"view-3", "ProfilePage", 5000},
  };

  for (const auto& v : views) {
    std::printf(
        "[rum] StartView(%s, %s) — spinning %dms...\n", v.key, v.name, v.spin_ms
    );
    rum->StartView(v.key, v.name);
    spin(v.spin_ms);
    rum->StopView(v.key);
    std::printf("[rum] StopView(%s)\n", v.key);
  }

  // 7. Stop
  std::printf("\n[core] stopping...\n");
  core->Stop();
  std::printf("[core] stopped\n");

  // 8. Summary
  std::printf("\n=== summary ===\n");
  std::printf("  service     : profiling-test\n");
  std::printf("  env         : dev\n");
  std::printf("  views       : 3 (HomePage, SettingsPage, ProfilePage)\n");
  std::printf("  spin/view   : 5s\n");
  std::printf("  total spin  : ~15s\n");
  std::printf("  profiler    : symbolize=true (reads DD_API_KEY/DD_SITE from env)\n");
  std::printf("=== done ===\n");

  return 0;
}
