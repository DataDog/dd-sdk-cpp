// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.h"

#include <memory>

#include "core/core.hpp"
#include "core/types.hpp"
#include "core_glue.hpp"
#include "platform/filesystem.hpp"
#include "platform/http.hpp"

// NOLINTBEGIN(cppcoreguidelines-owning-memory)

static const uint32_t CORE_CONFIG_VERSION = 1;

static const dd_core_config_t DEFAULT_CORE_CONFIG = {
    CORE_CONFIG_VERSION,               // version (for ABI future-proofing)
    DD_TRACKING_CONSENT_PENDING,       // tracking_consent
    DD_SITE_US1,                       // site
    nullptr,                           // client_token
    nullptr,                           // service
    nullptr,                           // env
    nullptr,                           // application_version
    DD_BATCH_SIZE_MEDIUM,              // batch_size
    DD_UPLOAD_FREQUENCY_AVERAGE,       // upload_frequency
    DD_BATCH_PROCESSING_LEVEL_MEDIUM,  // batch_processing_level
    {
        false,   // internal_options.flush_http_requests_on_stop
        nullptr  // internal_options.custom_endpoint_url
    }
};

extern "C" {

void dd_core_config_init(
    dd_core_config_t* config, const char* client_token, const char* service,
    const char* env
) {
  if (!config) {
    return;
  }
  *config = DEFAULT_CORE_CONFIG;
  config->client_token = client_token;
  config->service = service;
  config->env = env;
}

void dd_core_config_set_initial_tracking_consent(
    dd_core_config_t* config, dd_tracking_consent_t value
) {
  if (!config) {
    return;
  }
  config->tracking_consent = value;
}

void dd_core_config_set_site(dd_core_config_t* config, dd_site_t value) {
  if (!config) {
    return;
  }
  config->site = value;
}

void dd_core_config_set_client_token(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  config->client_token = value;
}

void dd_core_config_set_service(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  config->service = value;
}

void dd_core_config_set_env(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  config->env = value;
}

void dd_core_config_set_application_version(
    dd_core_config_t* config, const char* value
) {
  if (!config) {
    return;
  }
  config->application_version = value;
}

void dd_core_config_set_batch_size(dd_core_config_t* config, dd_batch_size_t value) {
  if (!config) {
    return;
  }
  config->batch_size = value;
}

void dd_core_config_set_upload_frequency(
    dd_core_config_t* config, dd_upload_frequency_t value
) {
  if (!config) {
    return;
  }
  config->upload_frequency = value;
}

void dd_core_config_set_batch_processing_level(
    dd_core_config_t* config, dd_batch_processing_level_t value
) {
  if (!config) {
    return;
  }
  config->batch_processing_level = value;
}

dd_core_t* dd_core_create(const dd_core_config_t* config) {
  // If we've not been given a valid config, return null (which effectively serves as a
  // no-op interface, since the C API tolerates null arguments)
  if (!config) {
    return nullptr;
  }

  // If the config struct was not properly initialized, reject it and make no further
  // attempts to read from the provided struct
  if (config->version <= 0 || config->version > CORE_CONFIG_VERSION) {
    return nullptr;
  }

  // Likewise, if the config is missing any required values, reject it
  const bool has_client_token = config->client_token && config->client_token[0];
  const bool has_service = config->service && config->service[0];
  const bool has_env = config->env && config->env[0];
  if (!has_client_token || !has_service || !has_env) {
    return nullptr;
  }

  // Convert from dd_core_config_t to datadog::CoreConfig, as the implementation layer
  // uses the latter
  datadog::CoreConfig cpp_config = datadog::CoreConfig_FromC(*config);

  // Initialize core subsystems using the platform-specific implementations compiled
  // in this build
  auto subsystems = datadog::impl::CoreSubsystems::Init(cpp_config);
  if (!subsystems) {
    return nullptr;
  }

  // Create the impl::Core object
  auto impl = std::make_unique<datadog::impl::Core>(cpp_config, std::move(*subsystems));

  // Perform mandatory initialization routines that might fail
  if (!impl->Init()) {
    return nullptr;
  }

  // Wrap the core in a dynamically-allocated dd_core struct, which will own our
  // implementation via unique_ptr, ensuring cleanup as long as we delete the dd_core
  dd_core_t* core = new dd_core;
  core->impl = std::move(impl);
  return core;
}

void dd_core_destroy(dd_core_t* core) { delete core; }

void dd_core_set_tracking_consent(dd_core_t* core, dd_tracking_consent_t value) {
  if (core && core->impl) {
    core->impl->SetTrackingConsent(datadog::TrackingConsent_FromC(value));
  }
}

bool dd_core_start(dd_core_t* core) {
  if (core && core->impl) {
    return core->impl->Start();
  }
  return false;
}

void dd_core_stop(dd_core_t* core) {
  if (core && core->impl) {
    core->impl->Stop();
  }
}
}

// NOLINTEND(cppcoreguidelines-owning-memory)
