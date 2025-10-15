// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/rum.h"

#include <memory>

#include "core/core.hpp"
#include "core_glue.hpp"
#include "datadog/core.h"
#include "datadog/uuid.hpp"
#include "features/rum/rum.hpp"
#include "features/rum/types.hpp"
#include "rum_glue.hpp"

static const uint32_t RUM_CONFIG_VERSION = 1;

static const dd_rum_config_t DEFAULT_RUM_CONFIG = {
    RUM_CONFIG_VERSION,  // version
    dd_uuid_t{}          // application_id
};

// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

void dd_rum_config_init(dd_rum_config_t* config, const char* application_id) {
  // Require a valid config struct and valid string
  if (!config || !application_id) {
    return;
  }

  // Parse application ID as a UUID value
  const auto application_id_opt = datadog::UUID::Parse(application_id);
  const datadog::UUID uuid_value = application_id_opt.value_or(datadog::UUID::Zero);

  // Populate struct fields
  *config = DEFAULT_RUM_CONFIG;
  dd_uuid_set(&config->application_id, uuid_value.bytes.data());
}

void dd_rum_config_set_application_id(dd_rum_config_t* config, const char* value) {
  if (config && value) {
    const auto value_opt = datadog::UUID::Parse(value);
    const datadog::UUID uuid_value = value_opt.value_or(datadog::UUID::Zero);
    dd_uuid_set(&config->application_id, uuid_value.bytes.data());
  }
}

dd_rum_t* dd_rum_init(dd_core_t* core, const dd_rum_config_t* config) {
  // Require a valid core: note that in the C API, where we tolerate null arguments to
  // all API functions, returning NULL is effectively returning a no-op implementation
  if (!core || !core->impl) {
    return nullptr;
  }

  // Require a valid config: RUM requires an application ID at the very least, so we
  // can't fall back to defaults
  if (!config) {
    return nullptr;
  }

  // If the config has an unrecognized version, it's not properly initialized in a form
  // that we can safely read
  if (config->version <= 0 || config->version > RUM_CONFIG_VERSION) {
    // TODO(RUM-11363): Ensure that all invalid-argument cases at the API layer are
    // signalled to the user via local telemetry logging
    return nullptr;
  }

  // If the config doesn't specify an application ID as a valid, nonzero UUID, reject it
  if (dd_uuid_is_zero(&config->application_id)) {
    // TODO(RUM-11363): Log a warning message locally to inform the user of bad config
    return nullptr;
  }

  // Convert from dd_rum_config_t to datadog::RumConfig
  datadog::RumConfig cpp_config = datadog::RumConfig_FromC(*config);

  // Get essential state from the Core
  const datadog::platform::IClock& clock = core->impl->GetClock();

  // Initialize our RUM feature implementation
  auto rum_impl = std::make_shared<datadog::impl::Rum>(cpp_config, clock);

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->impl->RegisterFeature(rum_impl)) {
    return nullptr;
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the RUM feature
  dd_rum_t* rum = new dd_rum;
  rum->impl = std::move(rum_impl);
  return rum;
}

void dd_rum_destroy(dd_rum_t* rum) { delete rum; }
}

// NOLINTEND(cppcoreguidelines-owning-memory)
