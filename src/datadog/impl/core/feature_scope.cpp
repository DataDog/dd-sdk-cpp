// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_scope.hpp"

#include "datadog/impl/assert.hpp"

namespace datadog::impl {

FeatureScope::FeatureScope(
    CoreContextProvider& context_provider,
    const EventGeneratedFunc& event_generated_func,
    const DiagnosticLogger& in_diagnostic_logger
)
    : _context_provider(&context_provider),
      _event_generated_func(event_generated_func),
      diagnostic_logger(in_diagnostic_logger) {}

CoreContext FeatureScope::GetContext() const {
  DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
  return _context_provider->Get();
}

void FeatureScope::UpdateContext(const std::function<void(CoreContext&)>& callback) {
  DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
  _context_provider->Update(callback);
}

bool FeatureScope::WriteEvent(Block event, Block event_metadata) const {
  if (_event_generated_func) {
    return _event_generated_func(event, event_metadata);
  }
  return false;
}

}  // namespace datadog::impl
