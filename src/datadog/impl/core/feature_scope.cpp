// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature_scope.hpp"

#include "datadog/impl/core/util/assert.hpp"

namespace datadog::impl {

FeatureScope::FeatureScope(
    CoreContextProvider& context_provider,
    const EventWriter& event_writer,
    const MessagePublisher& message_publisher,
    const DiagnosticLogger& in_diagnostic_logger,
    FeatureScope::ExecutionMode mode,
    Queue<std::function<void()>>* context_queue
)
    : _context_provider(&context_provider),
      _event_generated_func(event_writer),
      _message_produced_func(message_publisher),
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
    const EventWriter& event_writer,
    const MessagePublisher& message_publisher,
    const DiagnosticLogger& diagnostic_logger,
    Queue<std::function<void()>>& context_queue
) {
  return FeatureScope(
      context_provider,
      event_writer,
      message_publisher,
      diagnostic_logger,
      FeatureScope::ExecutionMode::OnContextThread,
      &context_queue
  );
}

FeatureScope FeatureScope::CreateForTesting(
    CoreContextProvider& context_provider,
    const EventWriter& event_writer,
    const MessagePublisher& message_publisher,
    const DiagnosticLogger& diagnostic_logger
) {
  return FeatureScope(
      context_provider,
      event_writer,
      message_publisher,
      diagnostic_logger,
      FeatureScope::ExecutionMode::Synchronous,
      nullptr
  );
}

void FeatureScope::UpdateContext(const std::function<void(CoreContext&)>& callback) {
  DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");

  if (_mode == FeatureScope::ExecutionMode::Synchronous) {
    // Testing mode: execute synchronously on calling thread
    _context_provider->Update(callback);
    return;
  }

  // Production mode: queue for async execution on context thread
  DATADOG_ASSERT(
      _context_queue != nullptr, "context_queue is null in OnContextThread mode"
  );
  _context_queue->Push([this, callback]() {
    DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
    _context_provider->Update(callback);
  });
}

void FeatureScope::ExecuteOnContextThread(const ContextThreadFunc& func) {
  if (_mode == FeatureScope::ExecutionMode::Synchronous) {
    // Testing mode: execute synchronously on calling thread
    DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
    const CoreContext context = _context_provider->Get();
    func(context, _event_generated_func, _message_produced_func);
    return;
  }

  // Production mode: queue for async execution on context thread
  DATADOG_ASSERT(
      _context_queue != nullptr, "context_queue is null in OnContextThread mode"
  );
  _context_queue->Push([this, func]() {
    DATADOG_ASSERT(_context_provider, "FeatureScope has no _context_provider");
    const CoreContext context = _context_provider->Get();
    func(context, _event_generated_func, _message_produced_func);
  });
}

}  // namespace datadog::impl
