// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.h"

#include <iostream>
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
    dd_stderr_diagnostic_handler,      // diagnostic_handler
    nullptr,                           // diagnostic_handler_userdata
    DD_DIAGNOSTIC_LEVEL_WARNING,       // diagnostic_threshold
    DD_TRACKING_CONSENT_PENDING,       // tracking_consent
    {0},                               // event_storage_location
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

void dd_stderr_diagnostic_handler(const dd_diagnostic_message_t* message, void*) {
  static const char* level_names[] = {"DEBUG", "STATUS", "WARNING", "ERROR"};
  const size_t i = static_cast<size_t>(message->level);
  const char* level_name = i < std::size(level_names) ? level_names[i] : "";  // NOLINT
  std::cerr << "[DATADOG " << level_name << "] " << message->text << "\n";
}

void dd_core_config_init(
    dd_core_config_t* config,
    const char* client_token,
    const char* service,
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

void dd_core_config_set_diagnostic_handler(
    dd_core_config_t* config, dd_diagnostic_handler_t value
) {
  if (!config) {
    return;
  }
  config->diagnostic_handler = value;
}

void dd_core_config_set_diagnostic_threshold(
    dd_core_config_t* config, dd_diagnostic_level_t value
) {
  if (!config) {
    return;
  }
  config->diagnostic_threshold = value;
}

void dd_core_config_set_diagnostic_handler_userdata(
    dd_core_config_t* config, void* value
) {
  if (!config) {
    return;
  }
  config->diagnostic_handler_userdata = value;
}

void dd_core_config_set_initial_tracking_consent(
    dd_core_config_t* config, dd_tracking_consent_t value
) {
  if (!config) {
    return;
  }
  config->tracking_consent = value;
}

void dd_core_config_set_event_storage_location(
    dd_core_config_t* config, const char* value
) {
  if (!config) {
    return;
  }
  const size_t len = std::strlen(value);
  const size_t max_len = std::size(config->event_storage_location) - 1;
  if (len > max_len) {
    auto diagnostic_logger = datadog::impl::DiagnosticLogger::FromC(
        config->diagnostic_handler,
        config->diagnostic_handler_userdata,
        config->diagnostic_threshold
    );
    diagnostic_logger.Error(
        "Unable to accept value passed to dd_core_config_set_event_storage_location: "
        "length limit exceeded"
    );
    return;
  }
  std::strncpy(
      static_cast<char*>(config->event_storage_location),
      value,
      std::size(config->event_storage_location)
  );
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

  // Prepare a diagnostic logging interface that we can use to emit messages to be
  // handled by the application (or written to stderr by default)
  auto diagnostic_logger = datadog::impl::DiagnosticLogger::FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );

  // Likewise, if the config is missing any required values, reject it
  if (!config->client_token || !config->client_token[0]) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in dd_core_config_t"
    );
    return nullptr;
  }
  if (!config->service || !config->service[0]) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in dd_core_config_t"
    );
    return nullptr;
  }
  if (!config->env || !config->env[0]) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "dd_core_config_t"
    );
    return nullptr;
  }

  // Convert from dd_core_config_t to datadog::CoreConfig, as the implementation layer
  // uses the latter
  datadog::CoreConfig cpp_config = datadog::CoreConfig_FromC(*config);

  // Initialize core subsystems using the platform-specific implementations compiled
  // in this build
  auto subsystems_result = datadog::impl::CoreSubsystems::Init(cpp_config);
  if (!subsystems_result) {
    // If we fail to initialize subsystems, log an error and return a no-op Core
    auto err = subsystems_result.error().AddPrefix("SDK initialization failed");
    diagnostic_logger.Error(err.Format().c_str());
    return nullptr;
  }
  datadog::impl::CoreSubsystems subsystems = std::move(*subsystems_result);

  // Create the impl::Core object
  auto impl = std::make_unique<datadog::impl::Core>(cpp_config, std::move(subsystems));

  // Perform mandatory initialization routines that might fail
  if (!impl->Init()) {
    return nullptr;
  }

  // Wrap the core in a dynamically-allocated dd_core struct, which will own our
  // implementation via unique_ptr, ensuring cleanup as long as we delete the dd_core
  return new dd_core(std::move(impl), std::move(diagnostic_logger));
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
