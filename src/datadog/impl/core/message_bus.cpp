// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/message_bus.hpp"

namespace datadog::impl {

MessageBus::MessageBus(
    Executor& executor, std::vector<std::function<void(const FeatureMessage&)>> handlers
)
    : _handlers(std::move(handlers)),
      _queue(executor.CreateQueue<FeatureMessage>(
          {"message-bus", StopPolicy::Drain, /*start_suspended=*/true},
          [this](FeatureMessage& msg) {
            for (auto& handler : _handlers) {
              handler(msg);
            }
          }
      )) {}

bool MessageBus::Send(FeatureMessage msg) { return _queue->Push(std::move(msg)); }

void MessageBus::Resume() { _queue->Resume(); }

void MessageBus::Stop() {
  _queue->Close();
  _queue->WaitIdle();
}

}  // namespace datadog::impl
