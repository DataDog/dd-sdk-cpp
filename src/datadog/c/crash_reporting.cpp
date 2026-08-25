// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/crash_reporting.h"

#include <cstdio>
#include <memory>
#include <string_view>

#include "datadog/core.h"

#include "datadog/c/core_glue.hpp"
#include "datadog/c/crash_reporting_glue.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/crash_reporting/crash_handler.hpp"
#include "datadog/impl/crash_reporting/crash_handler_init.hpp"
#include "datadog/impl/crash_reporting/crash_reporting.hpp"
#include "datadog/impl/types/diagnostics.hpp"

static const uint32_t CRASH_REPORTING_CONFIG_VERSION = 1;

static const dd_crash_reporting_config_t DEFAULT_CRASH_REPORTING_CONFIG = {
    CRASH_REPORTING_CONFIG_VERSION,  // version
    ""                               // handler_exe_path (empty = auto-detect)
};

// This C API necessarily uses C-style idioms for memory management and strings.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-type-vararg)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

extern "C" {

void dd_crash_reporting_config_init(dd_crash_reporting_config_t* config) {
  // Require an input value
  if (!config) {
    return;
  }
  *config = DEFAULT_CRASH_REPORTING_CONFIG;
}

void dd_crash_reporting_config_set_handler_exe_path(
    dd_crash_reporting_config_t* config, const char* value
) {
  if (config) {
    if (value) {
      std::snprintf(
          config->handler_exe_path, sizeof(config->handler_exe_path), "%s", value
      );
    } else {
      config->handler_exe_path[0] = '\0';
    }
  }
}

dd_crash_reporting_t* dd_crash_reporting_init(
    dd_core_t* core, const dd_crash_reporting_config_t* config
) {
  // Require a valid core: note that in the C API, where we tolerate null arguments to
  // all API functions, returning NULL is effectively returning a no-op implementation
  if (!core || !core->impl) {
    return nullptr;
  }

  // If no config was given, use the default config
  if (!config) {
    config = &DEFAULT_CRASH_REPORTING_CONFIG;
  }

  // If we were given a config struct with an invalid version number, assume it was not
  // properly initialized and fall back to the default config
  if (config->version <= 0 || config->version > CRASH_REPORTING_CONFIG_VERSION) {
    core->diagnostic_logger.Warning(
        "dd_crash_reporting_init falling back to default config: application must "
        "initialize dd_crash_reporting_config_t value via "
        "dd_crash_reporting_config_init"
    );
    config = &DEFAULT_CRASH_REPORTING_CONFIG;
  }

  // Prepare a directory at <application-storage>/.datadog/.crashes/ to contain
  // crash-related artifacts
  auto storage = core->impl->InitializeArtifactStorage(".crashes");
  if (!storage) {
    return nullptr;
  }

  // Initialize an ICrashHandler implementation and get a pointer to the global handler
  // instance, which persists throughout the lifetime of the process: this will only
  // return a valid pointer the first time it's called, ensuring that only a single
  // instance of the CrashReporting feature is empowered to handle and upload crashes
  datadog::impl::ICrashHandler* handler = datadog::impl::CrashHandler::InitializeOnce(
      core->diagnostic_logger,
      core->impl->GetFilesystem(),
      storage->GetPath(),
      config->handler_exe_path,
      core->impl->GetIntakeOrigin()
  );

  // If handler initialization failed, or if crash reporting is being enabled for an SDK
  // instance after the first, return a no-op CrashReporting interface
  if (!handler) {
    return nullptr;
  }

  // Initialize our CrashReporting feature implementation
  auto crash_reporting_impl = std::make_shared<datadog::impl::CrashReporting>(
      *handler, core->impl->GetFilesystem(), storage->GetPath()
  );

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->impl->RegisterFeature(crash_reporting_impl)) {
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the crash reporting feature
  return new dd_crash_reporting(
      std::move(crash_reporting_impl), core->diagnostic_logger
  );
}

void dd_crash_reporting_destroy(dd_crash_reporting_t* crash_reporting) {
  delete crash_reporting;
}
}

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(cppcoreguidelines-pro-type-vararg)
// NOLINTEND(cppcoreguidelines-owning-memory)
