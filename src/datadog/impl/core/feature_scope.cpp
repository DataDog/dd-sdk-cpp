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
    const DiagnosticLogger& in_diagnostic_logger,
    FeatureScope::ExecutionMode mode,
    Queue<std::function<void()>>* context_queue
)
    : _context_provider(&context_provider),
      _event_generated_func(event_generated_func),
      _mode(mode),
      _context_queue(context_queue),
      diagnostic_logger(in_diagnostic_logger) {
  // Invariant: context_queue must be non-null when mode is OnContextThread
  DATADOG_ASSERT(
      mode != FeatureScope::ExecutionMode::OnContextThread || context_queue != nullptr,
      "context_queue must be non-null when ExecutionMode is OnContextThread"
  );
}

FeatureScope FeatureScope::Create(
    CoreContextProvider& context_provider,
    const EventGeneratedFunc& event_generated_func,
    const DiagnosticLogger& diagnostic_logger,
    Queue<std::function<void()>>& context_queue
) {
  return FeatureScope(
      context_provider,
      event_generated_func,
      diagnostic_logger,
      FeatureScope::ExecutionMode::OnContextThread,
      &context_queue
  );
}

FeatureScope FeatureScope::CreateForTesting(
    CoreContextProvider& context_provider,
    const EventGeneratedFunc& event_generated_func,
    const DiagnosticLogger& diagnostic_logger
) {
  return FeatureScope(
      context_provider,
      event_generated_func,
      diagnostic_logger,
      FeatureScope::ExecutionMode::Synchronous,
      nullptr
  );
}

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

void FeatureScope::ExecuteOnContextThread(const ContextThreadFunc& func) {
  if (_mode == FeatureScope::ExecutionMode::Synchronous) {
    // Testing mode: execute synchronously on calling thread
    DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
    const CoreContext context = _context_provider->Get();
    func(context, _event_generated_func);
    return;
  }

  // Production mode: queue for async execution on context thread
  DATADOG_ASSERT(
      _context_queue != nullptr, "context_queue is null in OnContextThread mode"
  );
  _context_queue->Push([this, func]() {
    DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
    const CoreContext context = _context_provider->Get();
    func(context, _event_generated_func);
  });
}

}  // namespace datadog::impl
