// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/core.h"

#include <iostream>
#include <memory>
#include <optional>

#include "datadog/c/core_glue.hpp"
#include "datadog/c/network_adapter_glue.hpp"
#include "datadog/c/string_arg.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/tracing/types.hpp"
#include "datadog/impl/core/types.hpp"
#include "datadog/impl/types/attribute/types.hpp"

// NOLINTBEGIN(cppcoreguidelines-owning-memory)

extern "C" {

void dd_stderr_diagnostic_handler(const dd_diagnostic_message_t* message, void*) {
  static const char* level_names[] = {"DEBUG", "STATUS", "WARNING", "ERROR"};
  const size_t i = static_cast<size_t>(message->level);
  const char* level_name = i < std::size(level_names) ? level_names[i] : "";  // NOLINT
  std::cerr << "[DATADOG " << level_name << "] " << message->text << "\n";
}

dd_core_config_t* dd_core_config_create(
    const char* client_token, const char* service, const char* env
) {
  // Require all three values: the caller can still supply empty strings, which
  // dd_core_create will reject with a diagnostic message
  if (!client_token || !service || !env) {
    return nullptr;
  }
  return new dd_core_config(datadog::CoreConfig(client_token, service, env));
}

void dd_core_config_destroy(dd_core_config_t* config) { delete config; }

void dd_core_config_set_diagnostic_handler(
    dd_core_config_t* config, dd_diagnostic_handler_t value
) {
  if (config) {
    config->diagnostic_handler = value;
    config->UpdateDiagnosticHandler();
  }
}

void dd_core_config_set_diagnostic_handler_userdata(
    dd_core_config_t* config, void* value
) {
  if (config) {
    config->diagnostic_handler_userdata = value;
    config->UpdateDiagnosticHandler();
  }
}

void dd_core_config_set_diagnostic_threshold(
    dd_core_config_t* config, dd_diagnostic_level_t value
) {
  if (config) {
    config->config.SetDiagnosticThreshold(datadog::DiagnosticLevel_FromC(value));
  }
}

void dd_core_config_set_application_storage_path(
    dd_core_config_t* config, const char* value
) {
  if (config) {
    config->config.SetApplicationStoragePath(to_string_view(value));
  }
}

void dd_core_config_set_site(dd_core_config_t* config, dd_site_t value) {
  if (config) {
    config->config.SetSite(datadog::Site_FromC(value));
  }
}

void dd_core_config_set_client_token(dd_core_config_t* config, const char* value) {
  if (config) {
    config->config.SetClientToken(to_string_view(value));
  }
}

void dd_core_config_set_service(dd_core_config_t* config, const char* value) {
  if (config) {
    config->config.SetService(to_string_view(value));
  }
}

void dd_core_config_set_env(dd_core_config_t* config, const char* value) {
  if (config) {
    config->config.SetEnv(to_string_view(value));
  }
}

void dd_core_config_set_version(dd_core_config_t* config, const char* value) {
  if (config) {
    config->config.SetVersion(to_string_view(value));
  }
}

void dd_core_config_set_variant(dd_core_config_t* config, const char* value) {
  if (config) {
    config->config.SetVariant(to_string_view(value));
  }
}

void dd_core_config_set_batch_size(dd_core_config_t* config, dd_batch_size_t value) {
  if (config) {
    config->config.SetBatchSize(datadog::BatchSize_FromC(value));
  }
}

void dd_core_config_set_upload_frequency(
    dd_core_config_t* config, dd_upload_frequency_t value
) {
  if (config) {
    config->config.SetUploadFrequency(datadog::UploadFrequency_FromC(value));
  }
}

void dd_core_config_set_batch_processing_level(
    dd_core_config_t* config, dd_batch_processing_level_t value
) {
  if (config) {
    config->config.SetBatchProcessingLevel(datadog::BatchProcessingLevel_FromC(value));
  }
}

void dd_core_config_internal_flush_http_requests_on_stop(dd_core_config_t* config) {
  if (config) {
    config->config.Internal_FlushHttpRequestsOnStop();
  }
}

void dd_core_config_internal_set_custom_endpoint_url(
    dd_core_config_t* config, const char* value
) {
  if (config) {
    config->config.Internal_UseCustomEndpoint(to_string_view(value));
  }
}

void dd_core_config_internal_set_source(dd_core_config_t* config, const char* value) {
  if (config) {
    config->config.Internal_SetSource(to_string_view(value));
  }
}

void dd_core_config_internal_set_sdk_version(
    dd_core_config_t* config, const char* value
) {
  if (config) {
    config->config.Internal_SetSdkVersion(to_string_view(value));
  }
}

void dd_core_config_internal_set_ca_certificates(
    dd_core_config_t* config, const char* data, size_t size
) {
  if (config) {
    // Treat a null pointer as empty data regardless of size, restoring the default
    const std::string_view pem =
        data ? std::string_view{data, size} : std::string_view{};
    config->config.Internal_SetCaCertificates(pem);
  }
}

dd_core_t* dd_core_create(
    const dd_core_config_t* config, dd_tracking_consent_t tracking_consent
) {
  // If we've not been given a config, return null (which effectively serves as a no-op
  // interface, since the C API tolerates null arguments)
  if (!config) {
    return nullptr;
  }

  // Validate the config and create the core implementation, which logs the reason for
  // any failure through the config's diagnostic handler
  auto impl = datadog::impl::Core::Create(
      config->config, datadog::TrackingConsent_FromC(tracking_consent)
  );
  if (!impl) {
    return nullptr;
  }

  // Wrap the core in a dynamically-allocated dd_core struct, which will own our
  // implementation via unique_ptr, ensuring cleanup as long as we delete the dd_core
  datadog::impl::DiagnosticLogger diagnostic_logger = impl->GetDiagnosticLogger();
  return new dd_core(std::move(impl), std::move(diagnostic_logger));
}

void dd_core_destroy(dd_core_t* core) { delete core; }

dd_network_adapter_t* dd_core_create_network_adapter(
    dd_core_t* core,
    dd_trace_injection_policy_t policy,
    float sample_rate,
    const dd_first_party_hosts_t* custom_first_party_hosts
) {
  if (!core || !core->impl) {
    // No Core implementation; unable to create an adapter.
    return nullptr;
  }

  std::optional<datadog::FirstPartyHosts> hosts;
  if (custom_first_party_hosts) {
    hosts = datadog::FirstPartyHosts_FromC(*custom_first_party_hosts);
  }
  return new dd_network_adapter(core->impl->CreateNetworkAdapter(
      datadog::TraceInjectionPolicy_FromC(policy), sample_rate, std::move(hosts)
  ));
}

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

// NOLINTEND(cppcoreguidelines-owning-memory)
