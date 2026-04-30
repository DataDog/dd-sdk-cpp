// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <functional>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

namespace datadog::impl {

/**
 * Callback that writes an event to storage from the context thread.
 *
 * @param event Arbitrary bytes. Must be non-empty. Will be copied into the storage
 *  queue. Eventually written to a batch with TLVBlockType::Event.
 * @param event_metadata Optional metadata to accompany the event; will be copied. When
 *  an event that has metadata is eventually written, the metadata will be prepended as
 *  a block of type TLVBlockType::Metadata.
 * @param bypass_tracking_consent When `true`, the event is always written to the
 *  granted-consent storage directory, regardless of the current `TrackingConsent`
 *  value. Intended for use by crash reporting, which must apply the consent that was
 *  active at crash time rather than the current live consent.
 * @returns whether the event was successfuly enqueued for storage.
 */
using EventWriter = std::function<
    bool(Block event, Block event_metadata, bool bypass_tracking_consent)>;

/**
 * Callback that publishes a message to the message bus from the context thread.
 */
using MessagePublisher = std::function<bool(FeatureMessage)>;

/**
 * Function executed on the context thread with access to a CoreContext snapshot and
 * both an EventWriter and a MessagePublisher, for generating events and producing
 * feature-to-feature messages, respectively.
 */
using ContextThreadFunc = std::function<void(
    const CoreContext& context,
    const EventWriter& writer,
    const MessagePublisher& publisher
)>;

/**
 * Interface provided to each feature in order to give that feature access to core SDK
 * functionality.
 */
class FeatureScope {
 private:
  /**
   * Determines how functions submitted by Features will be executed. In production
   * usage, mode is always OnContextThread.
   */
  enum class ExecutionMode : uint8_t {
    OnContextThread,  // Used in production: functions are queued for async execution
    Synchronous,      // Used for testing: functions are executed synchronously
  };

  CoreContextProvider* _context_provider;
  EventWriter _event_generated_func;
  MessagePublisher _message_produced_func;
  ExecutionMode _mode;
  Queue<std::function<void()>>* _context_queue;

 private:
  /**
   * Private constructor. Use Create() or CreateForTesting() factory methods.
   */
  explicit FeatureScope(
      CoreContextProvider& context_provider,
      const EventWriter& event_writer,
      const MessagePublisher& message_publisher,
      const DiagnosticLogger& in_diagnostic_logger,
      ExecutionMode mode,
      Queue<std::function<void()>>* context_queue
  );

 public:
  DiagnosticLogger diagnostic_logger;

  // Noncopyable, but movable (so ownership can be transferred to Feature from Core)
  FeatureScope(const FeatureScope&) = delete;
  FeatureScope& operator=(const FeatureScope&) = delete;
  FeatureScope(FeatureScope&&) noexcept = default;
  FeatureScope& operator=(FeatureScope&&) noexcept = default;
  ~FeatureScope() = default;  // Add this line

  /**
   * Creates a FeatureScope for production use, where functions passed to UpdateContext
   * and ExecuteOnContextThread are enqueued on the provided `context_queue` for async
   * execution on the SDK's context thread.
   *
   * @param context_provider Provides thread-safe access to CoreContext
   * @param event_writer Callback invoked when features generate events
   * @param message_publisher Callback invoked when features produce messages
   * @param diagnostic_logger Logger for diagnostic messages
   * @param context_queue Queue containing thunks to be executed serially by the context
   *  thread; must outlive the FeatureScope
   */
  static FeatureScope Create(
      CoreContextProvider& context_provider,
      const EventWriter& event_writer,
      const MessagePublisher& message_publisher,
      const DiagnosticLogger& diagnostic_logger,
      Queue<std::function<void()>>& context_queue
  );

  /**
   * Creates a FeatureScope for use in unit tests, where functions passed to
   * UpdateContext and ExecuteOnContextThread are executed synchronously on the calling
   * thread.
   *
   * @param context_provider Provides thread-safe access to CoreContext
   * @param event_writer Callback invoked when features generate events
   * @param message_publisher Callback invoked when features produce messages
   * @param diagnostic_logger Logger for diagnostic messages
   */
  static FeatureScope CreateForTesting(
      CoreContextProvider& context_provider,
      const EventWriter& event_writer,
      const MessagePublisher& message_publisher,
      const DiagnosticLogger& diagnostic_logger
  );

  /**
   * Performs a thread-safe write to the SDK's global CoreContext value, allowing a
   * feature to populate up-to-date state that the core or other features may access.
   *
   * By convention, a feature should only modify the member(s) of CoreContext that it
   * exclusively owns: e.g. the RUM feature modifies the `RumFeatureContext` value, etc.
   *
   * In production usage, where _mode == OnContextThread, the update will be pushed
   * onto the context queue for async execution on the context thread.
   *
   * If initialized with FeatureScope::CreateForTesting(), where mode == Synchronous,
   * the update will be immediately executed on the calling thread.
   */
  void UpdateContext(const std::function<void(CoreContext&)>& callback);

  /**
   * Executes a function on the context thread, providing it with a CoreContext
   * snapshot and an EventWriter for generating events.
   *
   * Given a function that accepts an immutable snapshot of the CoreContext (taken at
   * execution time) and a callback that can be used to produce events, ensures that the
   * function is executed at the appropriate time.
   *
   * In production usage, where _mode == OnContextThread, the function will be pushed
   * onto the context queue, which the context thread processes serially until SDK
   * shutdown.
   *
   * If initialized with FeatureScope::CreateForTesting(), where mode == Synchronous,
   * the function will be immediately executed on the calling thread.
   */
  void ExecuteOnContextThread(const ContextThreadFunc& func);
};

}  // namespace datadog::impl
