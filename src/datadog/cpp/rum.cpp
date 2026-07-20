// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/rum.hpp"

#include <cctype>

#include "datadog/core.hpp"

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"
#include "datadog/impl/core/util/validation.hpp"
#include "datadog/impl/rum/rum.hpp"

namespace datadog {

RumConfig::RumConfig(std::string_view in_application_id) {
  const auto uuid_opt = UUID::Parse(in_application_id);
  application_id = uuid_opt.value_or(UUID::Zero);
}

RumConfig::RumConfig(const UUID& in_application_id)
    : application_id(in_application_id) {}

RumConfig::RumConfig(const RumConfig&) noexcept = default;
RumConfig& RumConfig::operator=(const RumConfig&) noexcept = default;
RumConfig::RumConfig(RumConfig&&) noexcept = default;
RumConfig& RumConfig::operator=(RumConfig&&) noexcept = default;

RumConfig& RumConfig::SetApplicationId(std::string_view value) {
  const auto uuid_opt = UUID::Parse(value);
  application_id = uuid_opt.value_or(UUID::Zero);
  return *this;
}

RumConfig& RumConfig::SetApplicationId(const UUID& value) {
  application_id = value;
  return *this;
}

RumConfig& RumConfig::SetSessionSampleRate(float value) {
  session_sample_rate = value;
  return *this;
}

Rum::Rum(Rum::PrivateCtorTag)
    : _impl(nullptr),
      _diagnostic_handler(nullptr),
      _diagnostic_threshold(DiagnosticLevel::Error) {}

Rum::Rum(
    std::shared_ptr<impl::Rum>&& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    Rum::PrivateCtorTag
)
    : _impl(std::move(impl)),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {}

Rum::~Rum() = default;

std::shared_ptr<Rum> Rum::Register(
    const std::shared_ptr<Core>& core, const RumConfig& config
) {
  // Return a no-op Rum interface if called without a valid core
  if (!core || !core->_impl) {
    return std::make_shared<Rum>(Rum::PrivateCtorTag{});
  }

  // If we don't have all required config values, return a no-op Rum interface
  if (config.application_id == UUID::Zero) {
    impl::DiagnosticLogger diagnostic_logger{
        core->_diagnostic_handler, core->_diagnostic_threshold
    };
    diagnostic_logger.Error(
        "RUM initialization failed: application_id value supplied via RumConfig must "
        "be a valid, nonzero UUID"
    );
    return std::make_shared<Rum>(Rum::PrivateCtorTag{});
  }

  // Get essential state from the Core
  const platform::IClock& clock = core->_impl->GetClock();

  // Initialize our RUM feature implementation
  auto rum_impl = std::make_shared<impl::Rum>(config, clock);

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->_impl->RegisterFeature(rum_impl)) {
    return std::make_shared<Rum>(Rum::PrivateCtorTag{});
  }

  // Initialize and return the API object that represents our user-facing interface
  // for the RUM feature
  return std::make_shared<Rum>(
      std::move(rum_impl),
      core->_diagnostic_handler,
      core->_diagnostic_threshold,
      Rum::PrivateCtorTag{}
  );
}

void Rum::AddAttribute(std::string_view name, const Attribute& value) {
  if (_impl) {
    _impl->AddAttribute(name, value);
  }
}

void Rum::RemoveAttribute(std::string_view name) {
  if (_impl) {
    _impl->RemoveAttribute(name);
  }
}

void Rum::StopSession() {
  if (_impl) {
    _impl->StopSession();
  }
}

void Rum::StartView(
    std::string_view key, std::string_view name, const Attribute& attributes
) {
  // Require a valid view key
  if (key.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StartView call ignored: application must supply a non-empty view key"
    );
    return;
  }

  if (_impl) {
    _impl->StartView(key, name, attributes);
  }
}

void Rum::AddViewAttribute(std::string_view name, const Attribute& value) {
  // We allow any string (including "") as an attribute name, so no need to validate

  if (_impl) {
    _impl->AddViewAttribute(name, value);
  }
}

void Rum::RemoveViewAttribute(std::string_view name) {
  // We allow any string (including "") as an attribute name, so no need to validate

  if (_impl) {
    _impl->RemoveViewAttribute(name);
  }
}

void Rum::StopView(std::string_view key, const Attribute& attributes) {
  // Require a valid view key
  if (key.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StopView call ignored: application must supply a non-empty view key"
    );
    return;
  }

  if (_impl) {
    _impl->StopView(key, attributes);
  }
}

void Rum::AddAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  // Require a valid action name
  if (name.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::AddAction call ignored: application must supply a non-empty action name"
    );
    return;
  }

  if (_impl) {
    _impl->AddAction(type, name, attributes);
  }
}

void Rum::StartAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  // Require a valid action name
  if (name.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StartAction call ignored: application must supply a non-empty action name"
    );
    return;
  }

  if (_impl) {
    _impl->StartAction(type, name, attributes);
  }
}

void Rum::StopAction(
    RumActionType type, std::string_view name, const Attribute& attributes
) {
  if (_impl) {
    // The RUM implementation doesn't actually use the provided RumActionType value when
    // stopping an action: it just stops the currently-active action without matching on
    // action type. We accept the parameter anyway for consistency.
    (void)type;
    _impl->StopAction(name, attributes);
  }
}

void Rum::StartResource(
    std::string_view key,
    RumResourceMethod method,
    std::string_view url,
    const Attribute& attributes
) {
  // Require a non-empty resource key and URL
  if (key.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StartResource call ignored: application must supply a non-empty resource "
        "key"
    );
    return;
  }
  if (url.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StartResource call ignored: application must supply a non-empty URL"
    );
    return;
  }

  if (_impl) {
    _impl->StartResource(key, impl::RumRequestDetails{method, url}, attributes);
  }
}

void Rum::StopResource(
    std::string_view key,
    int32_t status_code,
    int64_t size,
    RumResourceType type,
    const Attribute& attributes
) {
  // Require a non-empty resource key
  if (key.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StopResource call ignored: application must supply a non-empty resource "
        "key"
    );
    return;
  }

  if (_impl) {
    _impl->StopResource(
        key, impl::RumResponseDetails{status_code, size, type}, std::nullopt, attributes
    );
  }
}

void Rum::StopResourceWithError(
    std::string_view key,
    std::string_view error_message,
    std::string_view error_type,
    std::string_view error_stack_trace,
    bool is_network_error,
    int32_t status_code,
    const Attribute& attributes
) {
  // Require a non-empty resource key
  if (key.empty()) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StopResourceWithError call ignored: application must supply a non-empty "
        "resource key"
    );
    return;
  }

  if (_impl) {
    // If no error message is given, allow it (as the schema for RUM error events does
    // not forbid empty messages) but log a warning
    if (error_message.empty()) {
      impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
          "Rum::StopResourceWithError recording an error with no message: application "
          "should supply a non-empty error message"
      );
    }

    _impl->StopResource(
        key,
        impl::RumResponseDetails{status_code},
        impl::RumErrorDetails{
            error_message, error_type, error_stack_trace, is_network_error
        },
        attributes
    );
  }
}

void Rum::AddError(
    RumErrorSource source,
    std::string_view message,
    std::string_view type,
    std::string_view stack_trace,
    const Attribute& attributes
) {
  if (_impl) {
    // If no error message is given, allow it (as the schema for RUM error events does
    // not forbid empty messages) but log a warning
    if (message.empty()) {
      impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
          "Rum::AddError recording an error with no message: application should supply "
          "a non-empty error message"
      );
    }

    _impl->AddError(
        source, impl::RumErrorDetails{message, type, stack_trace}, attributes
    );
  }
}

void Rum::AddLongTask(Duration duration, const Attribute& attributes) {
  if (_impl) {
    // The schema for RUM long_task events requires a positive duration; reject it,
    // logging a warning
    if (duration <= Duration::zero()) {
      impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
          "Rum::AddLongTask call ignored: application must supply a positive duration"
      );
      return;
    }

    _impl->AddLongTask(duration, attributes);
  }
}

void Rum::StartOperation(
    std::string_view name, std::string_view operation_key, const Attribute& attributes
) {
  // Backend rejects blank/empty names with its own non-empty precondition;
  // drop client-side to match.
  if (impl::IsBlankString(name)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Error(
        "Rum::StartOperation call ignored: application must supply a non-empty "
        "operation name"
    );
    return;
  }

  // Warn if the name fails the backend's `[\w.@$-]*` pattern, but always
  // emit: the backend is the source of truth on character-set policy.
  if (!impl::HasOnlyAllowedOperationNameCharacters(name)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::StartOperation: operation name does not match the "
        "backend-accepted pattern [\\w.@$-]* (letters, digits, _ . @ $ -). The event "
        "will still be sent and may be rejected by the backend."
    );
  }

  // If operation_key is provided, it must be non-blank
  if (!operation_key.empty() && impl::IsBlankString(operation_key)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Error(
        "Rum::StartOperation call ignored: operation_key, if provided, must be "
        "a non-empty string"
    );
    return;
  }

  // Empty operation_key means "no key"
  std::optional<std::string_view> opt_key;
  if (!operation_key.empty()) {
    opt_key = operation_key;
  }

  if (_impl) {
    _impl->StartOperation(name, opt_key, attributes);
  }
}

void Rum::SucceedOperation(
    std::string_view name, std::string_view operation_key, const Attribute& attributes
) {
  if (impl::IsBlankString(name)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Error(
        "Rum::SucceedOperation call ignored: application must supply a "
        "non-empty operation name"
    );
    return;
  }
  if (!impl::HasOnlyAllowedOperationNameCharacters(name)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::SucceedOperation: operation name does not match the "
        "backend-accepted pattern [\\w.@$-]* (letters, digits, _ . @ $ -). The event "
        "will still be sent and may be rejected by the backend."
    );
  }

  // If operation_key is provided, it must be non-blank
  if (!operation_key.empty() && impl::IsBlankString(operation_key)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Error(
        "Rum::SucceedOperation call ignored: operation_key, if provided, must "
        "be a non-empty string"
    );
    return;
  }

  // Empty operation_key means "no key"
  std::optional<std::string_view> opt_key;
  if (!operation_key.empty()) {
    opt_key = operation_key;
  }

  if (_impl) {
    _impl->StopOperation(name, opt_key, std::nullopt, attributes);
  }
}

void Rum::FailOperation(
    std::string_view name,
    RumOperationFailureReason failure_reason,
    std::string_view operation_key,
    const Attribute& attributes
) {
  if (impl::IsBlankString(name)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Error(
        "Rum::FailOperation call ignored: application must supply a non-empty "
        "operation name"
    );
    return;
  }
  if (!impl::HasOnlyAllowedOperationNameCharacters(name)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Warning(
        "Rum::FailOperation: operation name does not match the "
        "backend-accepted pattern [\\w.@$-]* (letters, digits, _ . @ $ -). The event "
        "will still be sent and may be rejected by the backend."
    );
  }

  // If operation_key is provided, it must be non-blank
  if (!operation_key.empty() && impl::IsBlankString(operation_key)) {
    impl::DiagnosticLogger{_diagnostic_handler, _diagnostic_threshold}.Error(
        "Rum::FailOperation call ignored: operation_key, if provided, must be "
        "a non-empty string"
    );
    return;
  }

  // Empty operation_key means "no key"
  std::optional<std::string_view> opt_key;
  if (!operation_key.empty()) {
    opt_key = operation_key;
  }

  if (_impl) {
    _impl->StopOperation(name, opt_key, failure_reason, attributes);
  }
}

}  // namespace datadog
