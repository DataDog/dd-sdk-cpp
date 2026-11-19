// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/crash_reporting.h"

#include <memory>

#include "datadog/core.h"

#include "datadog/c/core_glue.hpp"
#include "datadog/c/crash_reporting_glue.hpp"
#include "datadog/c/string_arg.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/crash_reporting/crash_reporting.hpp"

// This C API necessarily uses C-style idioms for memory management.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

dd_crash_reporting_config_t* dd_crash_reporting_config_create(void) {
  return new dd_crash_reporting_config{};
}

void dd_crash_reporting_config_destroy(dd_crash_reporting_config_t* config) {
  delete config;
}

void dd_crash_reporting_config_set_handler_exe_path(
    dd_crash_reporting_config_t* config, const char* value
) {
  if (config) {
    config->config.SetHandlerExePath(to_string_view(value));
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

  // Create and register the feature, using the default config if none was given
  const datadog::CrashReportingConfig cpp_config =
      config ? config->config : datadog::CrashReportingConfig();
  auto crash_reporting_impl = datadog::impl::CrashReporting::CreateAndRegister(
      *core->impl, cpp_config, core->diagnostic_logger
  );
  if (!crash_reporting_impl) {
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

// NOLINTEND(cppcoreguidelines-owning-memory)
