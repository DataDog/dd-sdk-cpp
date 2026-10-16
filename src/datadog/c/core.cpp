// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.h"

#include <iostream>
#include <memory>

#include "datadog/c/config_string.hpp"
#include "datadog/c/core_glue.hpp"
#include "datadog/impl/core/attribute/types.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/types.hpp"

// NOLINTBEGIN(cppcoreguidelines-owning-memory)
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay)

static const uint32_t CORE_CONFIG_VERSION = 1;

static const dd_core_config_t DEFAULT_CORE_CONFIG = {
    CORE_CONFIG_VERSION,               // version (for ABI future-proofing)
    dd_stderr_diagnostic_handler,      // diagnostic_handler
    nullptr,                           // diagnostic_handler_userdata
    DD_DIAGNOSTIC_LEVEL_WARNING,       // diagnostic_threshold
    DD_TRACKING_CONSENT_PENDING,       // tracking_consent
    "",                                // application_storage_path
    DD_SITE_US1,                       // site
    "",                                // client_token
    "",                                // service
    "",                                // env
    "",                                // application_version
    "",                                // variant
    DD_BATCH_SIZE_MEDIUM,              // batch_size
    DD_UPLOAD_FREQUENCY_AVERAGE,       // upload_frequency
    DD_BATCH_PROCESSING_LEVEL_MEDIUM,  // batch_processing_level
    {
        false,  // internal_options.flush_http_requests_on_stop
        "",     // internal_options.custom_endpoint_url
        "",     // internal_options.source
        ""      // internal_options.sdk_version
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
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->client_token,
      sizeof(config->client_token),
      client_token,
      logger,
      "client_token value passed to dd_core_config_init exceeds "
      "DATADOG_MAX_CLIENT_TOKEN_LEN (" DATADOG_CSTR(
          DATADOG_MAX_CLIENT_TOKEN_LEN
      ) ") and will be truncated"
  );
  assign_string_truncate(
      config->service,
      sizeof(config->service),
      service,
      logger,
      "service value passed to dd_core_config_init exceeds DATADOG_MAX_SERVICE_LEN "
      "(" DATADOG_CSTR(DATADOG_MAX_SERVICE_LEN) ") and will be truncated"
  );
  assign_string_truncate(
      config->env,
      sizeof(config->env),
      env,
      logger,
      "env value passed to dd_core_config_init exceeds DATADOG_MAX_ENV_LEN "
      "(" DATADOG_CSTR(DATADOG_MAX_ENV_LEN) ") and will be truncated"
  );
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

void dd_core_config_set_application_storage_path(
    dd_core_config_t* config, const char* value
) {
  if (!config || !value) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_strict(
      config->application_storage_path,
      sizeof(config->application_storage_path),
      value,
      logger,
      "application_storage_path value exceeds "
      "DATADOG_MAX_APPLICATION_STORAGE_PATH_LEN (" DATADOG_CSTR(
          DATADOG_MAX_APPLICATION_STORAGE_PATH_LEN
      ) ") and will not be accepted"
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
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->client_token,
      sizeof(config->client_token),
      value,
      logger,
      "client_token value exceeds DATADOG_MAX_CLIENT_TOKEN_LEN (" DATADOG_CSTR(
          DATADOG_MAX_CLIENT_TOKEN_LEN
      ) ") and will be truncated"
  );
}

void dd_core_config_set_service(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->service,
      sizeof(config->service),
      value,
      logger,
      "service value exceeds DATADOG_MAX_SERVICE_LEN (" DATADOG_CSTR(
          DATADOG_MAX_SERVICE_LEN
      ) ") and will be truncated"
  );
}

void dd_core_config_set_env(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->env,
      sizeof(config->env),
      value,
      logger,
      "env value exceeds DATADOG_MAX_ENV_LEN (" DATADOG_CSTR(
          DATADOG_MAX_ENV_LEN
      ) ") and will be truncated"
  );
}

void dd_core_config_set_version(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->application_version,
      sizeof(config->application_version),
      value,
      logger,
      "application version value exceeds DATADOG_MAX_VERSION_LEN (" DATADOG_CSTR(
          DATADOG_MAX_VERSION_LEN
      ) ") and will be truncated"
  );
}

void dd_core_config_set_variant(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->variant,
      sizeof(config->variant),
      value,
      logger,
      "variant value exceeds DATADOG_MAX_VARIANT_LEN (" DATADOG_CSTR(
          DATADOG_MAX_VARIANT_LEN
      ) ") and will be truncated"
  );
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

void dd_core_config_internal_flush_http_requests_on_stop(dd_core_config_t* config) {
  if (!config) {
    return;
  }
  config->internal_options.flush_http_requests_on_stop = true;
}

void dd_core_config_internal_set_custom_endpoint_url(
    dd_core_config_t* config, const char* value
) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->internal_options.custom_endpoint_url,
      sizeof(config->internal_options.custom_endpoint_url),
      value,
      logger,
      "custom endpoint URL value exceeds DATADOG_INTERNAL_MAX_CUSTOM_ENDPOINT_URL_LEN "
      "(" DATADOG_CSTR(
          DATADOG_INTERNAL_MAX_CUSTOM_ENDPOINT_URL_LEN
      ) ") and will be truncated"
  );
}

void dd_core_config_internal_set_source(dd_core_config_t* config, const char* value) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->internal_options.source,
      sizeof(config->internal_options.source),
      value,
      logger,
      "source value exceeds DATADOG_INTERNAL_MAX_SOURCE_LEN (" DATADOG_CSTR(
          DATADOG_INTERNAL_MAX_SOURCE_LEN
      ) ") and will be truncated"
  );
}

void dd_core_config_internal_set_sdk_version(
    dd_core_config_t* config, const char* value
) {
  if (!config) {
    return;
  }
  auto logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );
  assign_string_truncate(
      config->internal_options.sdk_version,
      sizeof(config->internal_options.sdk_version),
      value,
      logger,
      "SDK version value exceeds DATADOG_INTERNAL_MAX_SDK_VERSION_LEN (" DATADOG_CSTR(
          DATADOG_INTERNAL_MAX_SDK_VERSION_LEN
      ) ") and will be truncated"
  );
}

dd_core_t* dd_core_create(
    const dd_core_config_t* config, dd_tracking_consent_t tracking_consent
) {
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
  auto diagnostic_logger = datadog::impl::DiagnosticLogger_FromC(
      config->diagnostic_handler,
      config->diagnostic_handler_userdata,
      config->diagnostic_threshold
  );

  // Likewise, if the config is missing any required values, reject it
  if (!config->client_token[0]) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'client_token' "
        "value in dd_core_config_t"
    );
    return nullptr;
  }
  if (!config->service[0]) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'service' "
        "value in dd_core_config_t"
    );
    return nullptr;
  }
  if (!config->env[0]) {
    diagnostic_logger.Error(
        "SDK initialization failed: application must supply a non-empty 'env' value in "
        "dd_core_config_t"
    );
    return nullptr;
  }

  // Initialize core subsystems using the platform-specific implementations compiled
  // in this build
  auto subsystems = datadog::impl::CoreSubsystems::Init(diagnostic_logger);
  if (!subsystems.has_value()) {
    return nullptr;
  }

  // Convert from dd_core_config_t to datadog::CoreConfig, as the implementation layer
  // uses the latter
  datadog::CoreConfig cpp_config = datadog::CoreConfig_FromC(*config);

  // Create the impl::Core object
  auto impl = std::make_unique<datadog::impl::Core>(
      cpp_config,
      datadog::TrackingConsent_FromC(tracking_consent),
      std::move(*subsystems)
  );

  // Perform mandatory initialization routines that might fail: this may include
  // migration of event data from <old-pid>/ to <new-pid>/
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

void dd_core_set_user_info(
    dd_core_t* core,
    const char* id,
    const char* name,
    const char* email,
    const dd_attribute_t* extra_info
) {
  if (core && core->impl) {
    datadog::Attribute cpp_extra_info;
    if (extra_info && extra_info->type == DD_VALUE_TYPE_OBJECT) {
      cpp_extra_info = datadog::impl::AttributeConversion::CopyFromC(*extra_info);
    }
    core->impl->SetUserInfo(
        id ? std::string_view{id} : std::string_view{},
        name ? std::string_view{name} : std::string_view{},
        email ? std::string_view{email} : std::string_view{},
        cpp_extra_info
    );
  }
}

void dd_core_add_user_extra_info(dd_core_t* core, const dd_attribute_t* extra_info) {
  if (core && core->impl) {
    datadog::Attribute cpp_extra_info;
    if (extra_info && extra_info->type == DD_VALUE_TYPE_OBJECT) {
      cpp_extra_info = datadog::impl::AttributeConversion::CopyFromC(*extra_info);
    }
    core->impl->AddUserExtraInfo(cpp_extra_info);
  }
}

void dd_core_clear_user_info(dd_core_t* core) {
  if (core && core->impl) {
    core->impl->ClearUserInfo();
  }
}

void dd_core_set_account_info(
    dd_core_t* core, const char* id, const char* name, const dd_attribute_t* extra_info
) {
  if (core && core->impl) {
    datadog::Attribute cpp_extra_info;
    if (extra_info && extra_info->type == DD_VALUE_TYPE_OBJECT) {
      cpp_extra_info = datadog::impl::AttributeConversion::CopyFromC(*extra_info);
    }
    core->impl->SetAccountInfo(
        id ? std::string_view{id} : std::string_view{},
        name ? std::string_view{name} : std::string_view{},
        cpp_extra_info
    );
  }
}

void dd_core_add_account_extra_info(dd_core_t* core, const dd_attribute_t* extra_info) {
  if (core && core->impl) {
    datadog::Attribute cpp_extra_info;
    if (extra_info && extra_info->type == DD_VALUE_TYPE_OBJECT) {
      cpp_extra_info = datadog::impl::AttributeConversion::CopyFromC(*extra_info);
    }
    core->impl->AddAccountExtraInfo(cpp_extra_info);
  }
}

void dd_core_clear_account_info(dd_core_t* core) {
  if (core && core->impl) {
    core->impl->ClearAccountInfo();
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

// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
// NOLINTEND(cppcoreguidelines-owning-memory)
