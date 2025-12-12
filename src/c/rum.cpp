// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/rum.h"

#include <memory>

#include "attribute/types.hpp"
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
    dd_uuid_t{},         // application_id
    100.0f               // session_sample_rate
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

void dd_rum_config_set_session_sample_rate(dd_rum_config_t* config, float value) {
  if (config) {
    config->session_sample_rate = value;
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
    core->diagnostic_logger.Error(
        "RUM initialization failed: application must supply a valid dd_rum_config_t "
        "value to dd_rum_init"
    );
    return nullptr;
  }

  // If the config has an unrecognized version, it's not properly initialized in a form
  // that we can safely read
  if (config->version <= 0 || config->version > RUM_CONFIG_VERSION) {
    core->diagnostic_logger.Error(
        "RUM initialization failed: dd_rum_config_t value must be initialized via "
        "dd_rum_config_init"
    );
    return nullptr;
  }

  // If the config doesn't specify an application ID as a valid, nonzero UUID, reject it
  if (dd_uuid_is_zero(&config->application_id)) {
    core->diagnostic_logger.Error(
        "RUM initialization failed: application_id value supplied via dd_rum_config_t "
        "must be a valid, nonzero UUID"
    );
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
  // for the RUM feature, injecting a copy of the core's DiagnosticLogger so that future
  // RUM API calls can emit warnings etc.
  return new dd_rum(std::move(rum_impl), core->diagnostic_logger);
}

void dd_rum_destroy(dd_rum_t* rum) { delete rum; }

void dd_rum_add_attribute(
    dd_rum_t* rum, const char* name, const dd_attribute_t* value
) {
  // Permit no-op calls
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    rum->diagnostic_logger.Warning(
        "dd_rum_add_attribute call ignored: application must supply an attribute name"
    );
    return;
  }

  // Require a valid attribute value
  if (!value) {
    rum->diagnostic_logger.Warning(
        "dd_rum_add_attribute call ignored: application must supply an attribute value"
    );
    return;
  }

  // Copy to datadog::Attribute
  rum->impl->AddAttribute(name, datadog::impl::AttributeConversion::CopyFromC(*value));
}

void dd_rum_remove_attribute(dd_rum_t* rum, const char* name) {
  // Permit no-op calls
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    rum->diagnostic_logger.Warning(
        "dd_rum_remove_attribute call ignored: application must supply an attribute "
        "name"
    );
    return;
  }

  rum->impl->RemoveAttribute(name);
}

void dd_rum_stop_session(dd_rum_t* rum) {
  if (!rum || !rum->impl) {
    return;
  }
  rum->impl->StopSession();
}

void dd_rum_start_view(dd_rum_t* rum, const char* key, const char* name) {
  dd_rum_start_view_obj(rum, key, name, nullptr);
}

void dd_rum_start_view_obj(
    dd_rum_t* rum, const char* key, const char* name, const dd_attribute_t* attributes
) {
  // Permit no-op calls
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty string for view key
  if (!key || !key[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_start_view call ignored: application must supply a non-empty view key"
    );
    return;
  }

  // Construct a string_view of our name value, leaving it empty if no name string given
  std::string_view cpp_name{};
  if (name) {
    cpp_name = name;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Defer to the feature implementation
  rum->impl->StartView(key, cpp_name, cpp_attributes);
}

void dd_rum_add_view_attribute(
    dd_rum_t* rum, const char* name, const dd_attribute_t* value
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    rum->diagnostic_logger.Warning(
        "dd_rum_add_view_attribute call ignored: application must supply an attribute "
        "name"
    );
    return;
  }

  // Require a valid attribute value
  if (!value) {
    rum->diagnostic_logger.Warning(
        "dd_rum_add_view_attribute call ignored: application must supply an attribute "
        "value"
    );
    return;
  }

  datadog::Attribute cpp_value = datadog::impl::AttributeConversion::CopyFromC(*value);
  rum->impl->AddViewAttribute(name, cpp_value);
}

void dd_rum_remove_view_attribute(dd_rum_t* rum, const char* name) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid string, but allow "" as an attribute name
  if (!name) {
    rum->diagnostic_logger.Warning(
        "dd_rum_remove_view_attribute call ignored: application must supply an "
        "attribute name"
    );
    return;
  }

  rum->impl->RemoveViewAttribute(name);
}

void dd_rum_stop_view(dd_rum_t* rum, const char* key) {
  dd_rum_stop_view_obj(rum, key, nullptr);
}

void dd_rum_stop_view_obj(
    dd_rum_t* rum, const char* key, const dd_attribute_t* attributes
) {
  // Permit no-op calls
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty string for view key
  if (!key || !key[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_stop_view call ignored: application must supply a non-empty view key"
    );
    return;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Defer to the feature implementation
  rum->impl->StopView(key, cpp_attributes);
}

void dd_rum_add_action(
    dd_rum_t* rum,
    dd_rum_action_type_t type,
    const char* name,
    dd_attribute_t* attributes
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty action name
  if (!name || !name[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_add_action call ignored: application must supply a non-empty action "
        "name"
    );
    return;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  rum->impl->AddAction(datadog::RumActionType_FromC(type), name, cpp_attributes);
}

void dd_rum_start_action(
    dd_rum_t* rum,
    dd_rum_action_type_t type,
    const char* name,
    dd_attribute_t* attributes
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty action name
  if (!name || !name[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_start_action call ignored: application must supply a non-empty action "
        "name"
    );
    return;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  rum->impl->StartAction(datadog::RumActionType_FromC(type), name, cpp_attributes);
}

void dd_rum_stop_action(
    dd_rum_t* rum,
    dd_rum_action_type_t type,
    const char* name,
    dd_attribute_t* attributes
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // If we've been given a name, use it to rename the current action on stop; but allow
  // either NULL or empty-string
  std::string_view cpp_name;
  if (name) {
    cpp_name = name;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // The RUM implementation doesn't actually use the provided RumActionType value when
  // stopping an action: it just stops the currently-active action without matching on
  // action type. We accept the parameter anyway for consistency.
  (void)type;
  rum->impl->StopAction(cpp_name, cpp_attributes);
}

void dd_rum_start_resource(
    dd_rum_t* rum,
    const char* key,
    dd_rum_resource_method_t method,
    const char* url,
    dd_attribute_t* attributes
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty resource key and URL
  if (!key || !key[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_start_resource call ignored: application must supply a non-empty "
        "resource key"
    );
    return;
  }
  if (!url || !url[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_start_resource call ignored: application must supply a non-empty URL"
    );
    return;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Call StartResource on our RUM feature implementation, passing HTTP request details
  const datadog::impl::RumRequestDetails request{
      datadog::RumResourceMethod_FromC(method), url
  };
  rum->impl->StartResource(key, request, cpp_attributes);
}

void dd_rum_stop_resource(
    dd_rum_t* rum,
    const char* key,
    int32_t status_code,
    int64_t size,
    dd_rum_resource_type_t type,
    dd_attribute_t* attributes
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty resource key
  if (!key || !key[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_stop_resource call ignored: application must supply a non-empty "
        "resource key"
    );
    return;
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Call StopResource on our RUM feature implementation, passing HTTP response details
  const datadog::impl::RumResponseDetails response{
      status_code, size, datadog::RumResourceType_FromC(type)
  };
  rum->impl->StopResource(key, response, std::nullopt, cpp_attributes);
}

void dd_rum_stop_resource_with_error(
    dd_rum_t* rum,
    const char* key,
    const char* error_message,
    const char* error_type,
    const char* error_stack_trace,
    bool is_network_error,
    int32_t status_code,
    dd_attribute_t* attributes
) {
  // If the underlying feature is NULL, this call is a no-op
  if (!rum || !rum->impl) {
    return;
  }

  // Require a valid, non-empty resource key
  if (!key || !key[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_stop_resource_with_error call ignored: application must supply a "
        "non-empty resource key"
    );
    return;
  }

  // If no error message is given, allow it (as the schema for RUM error events does not
  // forbid empty messages) but log a warning
  if (!error_message || !error_message[0]) {
    rum->diagnostic_logger.Warning(
        "dd_rum_stop_resource_with_error recording an error with no message: "
        "application should supply a non-empty error message"
    );
  }

  // If we've been given a valid object attribute, convert it to the equivalent C++ type
  datadog::Attribute cpp_attributes;  // Default-initialized to Attribute::Null()
  if (attributes && attributes->type == DD_VALUE_TYPE_OBJECT) {
    cpp_attributes = datadog::impl::AttributeConversion::CopyFromC(*attributes);
  }

  // Call StopResource on our RUM feature implementation, passing error details
  const datadog::impl::RumResponseDetails response{status_code};
  const datadog::impl::RumErrorDetails error{
      error_message ? error_message : std::string_view{},
      error_type ? error_type : std::string_view{},
      error_stack_trace ? error_stack_trace : std::string_view{},
      is_network_error
  };
  rum->impl->StopResource(key, response, error, cpp_attributes);
}
}

// NOLINTEND(cppcoreguidelines-owning-memory)
