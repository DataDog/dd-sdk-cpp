// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <functional>
#include <vector>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Holds the shared state for the messaging subsystem: a thread-safe queue and the
 * fixed list of subscriber callbacks. The background thread that drains the queue and
 * invokes the handlers lives outside this class (see `MessagingThreadMain`), following
 * the same ownership pattern used by `context_thread`, `storage_thread`, and
 * `upload_thread`.
 *
 * Handlers are supplied at construction time and are never modified afterward, so
 * `MessagingThreadMain` can iterate `_handlers` without any synchronization.
 *
 * `Send()` enqueues a message non-blocking from any thread and returns false if the
 * queue has been stopped. Call `Stop()` to signal shutdown; the messaging thread will
 * drain any remaining messages before exiting.
 */
class MessageBus {
 public:
  explicit MessageBus(std::vector<std::function<void(const FeatureMessage&)>> handlers);

  ~MessageBus() = default;
  MessageBus(const MessageBus&) = delete;
  MessageBus& operator=(const MessageBus&) = delete;
  MessageBus(MessageBus&&) = delete;
  MessageBus& operator=(MessageBus&&) = delete;

  /**
   * Enqueues `msg` for delivery to all handlers. Non-blocking. Returns false if the
   * queue has been stopped and the message was therefore dropped.
   */
  bool Send(FeatureMessage msg);

  /**
   * Signals the messaging thread to drain remaining messages and exit. Must be called
   * before the bus is destroyed; the caller must join the messaging thread after this
   * returns.
   */
  void Stop();

 private:
  friend void MessagingThreadMain(const DiagnosticLogger&, MessageBus&);

  Queue<FeatureMessage> _queue;
  std::vector<std::function<void(const FeatureMessage&)>> _handlers;
};

}  // namespace datadog::impl
