// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Writes events to storage from the context thread. There are two ways to write an
 * event, which differ in how the event's bytes reach the storage queue:
 *
 * - WriteCopy() takes the event as a `Block`, and copies the bytes into the storage
 *   queue, so the caller keeps its own copy. This suits small events, and bytes that
 *   the caller still needs.
 * - WriteMove() takes the event as a `std::vector<uint8_t>&&`, and moves the vector
 *   into the storage queue, which takes ownership of the bytes without copying them.
 *   This suits larger events that the caller has built to hand to storage, such as a
 *   profile. The caller must pass it with `std::move()`, and must not use it
 *   afterwards.
 *
 * With both methods, `event` must be non-empty, and is eventually written to a batch
 * with TLVBlockType::Event. `event_metadata` is optional metadata to accompany the
 * event, and is always copied. When an event that has metadata is eventually written,
 * the metadata is prepended as a block of type TLVBlockType::Metadata.
 * `bypass_tracking_consent`, when `true`, has the event always written to the
 * granted-consent storage directory, regardless of the current `TrackingConsent` value.
 * This is intended for use by crash reporting, which must apply the consent that was
 * active at crash time rather than the current live consent.
 *
 * Both methods return whether the event was successfully enqueued for storage. A moved
 * vector is consumed only if it was enqueued; if the result is false, the vector may
 * have been left untouched or emptied, so the caller must not rely on its content.
 *
 * An EventWriter is made from two functions, one for each method. Neither falls back to
 * the other, so whoever creates a writer decides how each kind of write is handled.
 */
class EventWriter {
 public:
  /** The function behind WriteCopy(): writes an event given as a `Block`. */
  using CopyingFunc = std::function<
      bool(Block event, Block event_metadata, bool bypass_tracking_consent)>;

  /** The function behind WriteMove(): writes an event given as a vector. */
  using MovingFunc = std::function<bool(
      std::vector<uint8_t>&& event, Block event_metadata, bool bypass_tracking_consent
  )>;

  /** Creates a writer that is empty, and so cannot be called. */
  EventWriter() = default;

  /**
   * Creates a writer from `write_copy`, which copies the bytes of an event given as a
   * `Block`, and `write_move`, which takes ownership of an event given as a vector.
   */
  EventWriter(CopyingFunc write_copy, MovingFunc write_move)
      : _write_copy(std::move(write_copy)), _write_move(std::move(write_move)) {}

  /** Writes an event, copying the bytes of `event` into the storage queue. */
  bool WriteCopy(
      Block event, Block event_metadata, bool bypass_tracking_consent
  ) const {
    return _write_copy(event, event_metadata, bypass_tracking_consent);
  }

  /** Writes an event, moving `event` into the storage queue instead of copying it. */
  bool WriteMove(
      std::vector<uint8_t>&& event, Block event_metadata, bool bypass_tracking_consent
  ) const {
    return _write_move(std::move(event), event_metadata, bypass_tracking_consent);
  }

 private:
  CopyingFunc _write_copy;
  MovingFunc _write_move;
};

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
   *
   * The function is taken by value, and relies on move semantics as it moves to the
   * queue. A function that owns a lot of data, such as a large buffer, can therefore
   * be given that data by moving it into the function's captures, as in
   * `[data = std::move(data)](...) { ... }`, and the data reaches the context thread
   * without being copied.
   */
  void ExecuteOnContextThread(ContextThreadFunc func);
};

}  // namespace datadog::impl
