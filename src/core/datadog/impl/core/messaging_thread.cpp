// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/messaging_thread.hpp"

namespace datadog::impl {

void MessagingThreadMain(const DiagnosticLogger& diagnostic_logger, MessageBus& bus) {
  diagnostic_logger.Debug("Messaging thread starting");

  while (auto msg = bus._queue.Pop()) {
    for (auto& handler : bus._handlers) {
      handler(*msg);
    }
  }

  diagnostic_logger.Debug("Messaging thread finished");
}

}  // namespace datadog::impl
