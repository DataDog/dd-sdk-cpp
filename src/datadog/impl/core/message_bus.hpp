// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>
#include <thread>
#include <vector>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/diagnostics.hpp"

namespace datadog::impl {

/**
 * Owns a background thread that delivers `FeatureMessage` values to a fixed set of
 * subscriber callbacks. The bus broadcasts every message to all handlers in
 * registration order.
 *
 * Handlers are supplied at construction time and never change after `Start` (which is
 * called by the constructor). This means no synchronization is required inside
 * `ThreadMain` — the handler list is immutable for the lifetime of the thread.
 *
 * `Send()` enqueues a message non-blocking from any thread and returns false if the bus
 * has already been stopped. `Stop()` drains all remaining queued messages and joins the
 * thread before returning, so callers can rely on all in-flight messages having been
 * delivered once `Stop()` returns.
 */
class MessageBus {
 public:
  /**
   * Constructs a `MessageBus` and starts the background delivery thread immediately.
   * `handlers` is the complete, immutable list of callbacks that will receive every
   * message; `logger` is used to emit debug messages at thread start and stop.
   */
  explicit MessageBus(
      std::vector<std::function<void(const FeatureMessage&)>> handlers,
      const DiagnosticLogger& logger
  );

  ~MessageBus();

  MessageBus(const MessageBus&) = delete;
  MessageBus& operator=(const MessageBus&) = delete;
  MessageBus(MessageBus&&) = delete;
  MessageBus& operator=(MessageBus&&) = delete;

  /**
   * Enqueues `msg` for delivery to all handlers. Non-blocking. Returns false if the bus
   * has been stopped and the message was therefore dropped.
   */
  bool Send(FeatureMessage msg);

  /**
   * Stops the bus: no further messages will be accepted, all messages already in the
   * queue will be delivered, and the background thread will be joined. Must be called
   * before the `MessageBus` is destroyed.
   */
  void Stop();

 private:
  void ThreadMain();

  Queue<FeatureMessage> _queue;
  std::vector<std::function<void(const FeatureMessage&)>> _handlers;
  DiagnosticLogger _logger;
  std::thread _thread;
};

}  // namespace datadog::impl
