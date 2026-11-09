// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "datadog/impl/core/executor.hpp"
#include "datadog/impl/core/feature_message.hpp"

namespace datadog::impl {

/**
 * Delivers FeatureMessages to a fixed list of subscriber callbacks, asynchronously, on
 * the executor's "message-bus" queue. Each message is broadcast to every handler in
 * registration order before the next message is delivered.
 *
 * Handlers are supplied at construction time and never modified afterward, so they can
 * be invoked without any synchronization.
 *
 * Delivery starts suspended: messages sent before Resume() are buffered, so that the
 * owner can accept messages before every subscriber is ready to receive them.
 *
 * `Send()` enqueues a message without blocking, from any thread, and returns false if
 * the bus has been stopped.
 */
class MessageBus {
 public:
  MessageBus(
      Executor& executor,
      std::vector<std::function<void(const FeatureMessage&)>> handlers
  );

  /** Equivalent to Stop(). */
  ~MessageBus() = default;
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
   * Starts delivering messages, including any buffered before this call. Idempotent.
   */
  void Resume();

  /**
   * Stops accepting messages, then blocks until every message accepted so far has been
   * delivered. Must not be called from a handler.
   */
  void Stop();

 private:
  // Declared before _queue: the queue's handler reads it until the queue is destroyed
  std::vector<std::function<void(const FeatureMessage&)>> _handlers;
  std::unique_ptr<SerialQueue<FeatureMessage>> _queue;
};

}  // namespace datadog::impl
