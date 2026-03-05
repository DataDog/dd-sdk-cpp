// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/context_thread.hpp"

namespace datadog::impl {

void ContextThreadMain(
    const DiagnosticLogger& diagnostic_logger,
    Queue<std::function<void()>>& queue,
    CoreContextProvider& /* context_provider */
) {
  diagnostic_logger.Debug("Context thread starting");

  // Process functions until the queue is stopped and drained (Pop() returns
  // std::nullopt)
  while (auto thunk = queue.Pop()) {
    (*thunk)();
  }

  diagnostic_logger.Debug("Context thread finished");
}

}  // namespace datadog::impl
