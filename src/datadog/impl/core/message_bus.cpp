// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/message_bus.hpp"

namespace datadog::impl {

MessageBus::MessageBus(
    std::vector<std::function<void(const FeatureMessage&)>> handlers,
    const DiagnosticLogger& logger
)
    : _handlers(std::move(handlers)),
      _logger(logger),
      _thread(&MessageBus::ThreadMain, this) {}

MessageBus::~MessageBus() {
  // The owner must call Stop() before destruction; if the thread is still joinable here
  // the queue was never stopped, which is a programming error.
  DATADOG_ASSERT(!_thread.joinable(), "MessageBus destroyed without calling Stop()");
}

bool MessageBus::Send(FeatureMessage msg) { return _queue.Push(std::move(msg)); }

void MessageBus::Stop() {
  _queue.Stop();
  _thread.join();
}

void MessageBus::ThreadMain() {
  _logger.Debug("Message bus thread starting");

  while (auto msg = _queue.Pop()) {
    for (auto& handler : _handlers) {
      handler(*msg);
    }
  }

  _logger.Debug("Message bus thread finished");
}

}  // namespace datadog::impl
