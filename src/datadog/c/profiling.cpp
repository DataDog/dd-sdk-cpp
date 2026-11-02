// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/profiling.h"

#include <memory>

#include "datadog/core.h"

#include "datadog/c/core_glue.hpp"
#include "datadog/c/profiling_glue.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/profiling/profiling.hpp"
#include "datadog/impl/types/diagnostics.hpp"

static const uint32_t PROFILING_CONFIG_VERSION = 1;

static const dd_profiling_config_t DEFAULT_PROFILING_CONFIG = {
    PROFILING_CONFIG_VERSION  // version
};

// This C API necessarily uses C-style idioms for memory management.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

void dd_profiling_config_init(dd_profiling_config_t* config) {
  // Require an input value
  if (!config) {
    return;
  }
  *config = DEFAULT_PROFILING_CONFIG;
}

dd_profiling_t* dd_profiling_init(
    dd_core_t* core, const dd_profiling_config_t* config
) {
  // Require a valid core: note that in the C API, where we tolerate null arguments to
  // all API functions, returning NULL is effectively returning a no-op implementation
  if (!core || !core->impl) {
    return nullptr;
  }

  // The config has no settings yet, so a missing or uninitialized one is harmless, but
  // we still tell the application about the latter
  if (config && (config->version <= 0 || config->version > PROFILING_CONFIG_VERSION)) {
    core->diagnostic_logger.Warning(
        "dd_profiling_init ignoring config: application must initialize "
        "dd_profiling_config_t value via dd_profiling_config_init"
    );
  }

  // Returns null (after logging why) if there is no profiler to use, or if
  // registration fails
  auto profiling_impl =
      datadog::impl::Profiling::CreateAndRegister(*core->impl, core->diagnostic_logger);
  if (!profiling_impl) {
    return nullptr;
  }

  return new dd_profiling(profiling_impl, core->diagnostic_logger);
}

void dd_profiling_destroy(dd_profiling_t* profiling) { delete profiling; }
}

// NOLINTEND(cppcoreguidelines-owning-memory)
