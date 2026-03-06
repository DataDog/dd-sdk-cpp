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
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/diagnostics.hpp"

namespace datadog::impl {

/**
 * Callback invoked by a feature when it generates an event that needs to be enqueued
 * for storage.
 *
 * @param event Arbitrary bytes. Must be non-empty. Will be copied into the storage
 *  queue. Eventually written to a batch with TLVBlockType::Event.
 * @param event_metadata Optional metadata to accompany the event; will be copied. When
 *  an event that has metadata is eventually written, the metadata will be prepended as
 *  a block of type TLVBlockType::Metadata.
 * @returns whether the event was successfuly enqueued for storage.
 */
using EventGeneratedFunc = std::function<bool(Block event, Block event_metadata)>;

/**
 * Callback that writes an event to storage from the context thread.
 *
 * Returns whether the event was successfully enqueued.
 */
using EventWriter = std::function<bool(Block event, Block event_metadata)>;

/**
 * Function executed on the context thread with access to a CoreContext snapshot and an
 * EventWriter for generating events.
 */
using ContextThreadFunc =
    std::function<void(const CoreContext& context, const EventWriter& writer)>;

/**
 * Interface provided to each feature in order to give that feature access to core SDK
 * functionality.
 */
class FeatureScope {
 private:
  /**
   * Determines how functions passed to OnContextThread will be evaluated. In
   * production usage, mode is always OnContextThread and _context_queue is always
   * non-null.
   */
  enum class ExecutionMode : uint8_t {
    OnContextThread,  // Used in production: functions are queued for async execution
    Synchronous,      // Used for testing: functions are executed synchronously
  };

  CoreContextProvider* _context_provider;
  EventGeneratedFunc _event_generated_func;
  ExecutionMode _mode;
  Queue<std::function<void()>>* _context_queue;

 private:
  /**
   * Private constructor. Use Create() or CreateForTesting() factory methods.
   */
  explicit FeatureScope(
      CoreContextProvider& context_provider,
      const EventGeneratedFunc& event_generated_func,
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
   * Creates a FeatureScope for production use, where ExecuteOnContextThread operations
   * are enqueued on the provided `context_queue` for async execution on the SDK's
   * context thread.
   *
   * @param context_provider Provides thread-safe access to CoreContext
   * @param event_generated_func Callback invoked when features generate events
   * @param diagnostic_logger Logger for diagnostic messages
   * @param context_queue Queue containing thunks to be executed serially by the context
   *  thread; must outlive the FeatureScope
   */
  static FeatureScope Create(
      CoreContextProvider& context_provider,
      const EventGeneratedFunc& event_generated_func,
      const DiagnosticLogger& diagnostic_logger,
      Queue<std::function<void()>>& context_queue
  );

  /**
   * Creates a FeatureScope for use in unit tests, where ExecuteOnContextThread
   * operations are executed synchronously on the calling thread.
   *
   * @param context_provider Provides thread-safe access to CoreContext
   * @param event_generated_func Callback invoked when features generate events
   * @param diagnostic_logger Logger for diagnostic messages
   */
  static FeatureScope CreateForTesting(
      CoreContextProvider& context_provider,
      const EventGeneratedFunc& event_generated_func,
      const DiagnosticLogger& diagnostic_logger
  );

  /**
   * Creates an immutable, thread-safe copy of the CoreContext, which contains all the
   * external information that a feature might need in order to generate fully-enriched
   * events.
   *
   * NOTE: Our current logging implementation is naively optimized to ensure
   * as-fast-as-possible best-case performance, and to minimize copying and allocations,
   * but it may exhibit variable and undesirable worst-case performance when there's
   * contention on the CoreContext - e.g. if we try to get a copy of the CoreContext in
   * the main thread while another thread is modifying it, we may end up blocking the
   * main thread for an unacceptable duration.
   *
   * The mobile SDKs optimize for predictable worst-case performance in the main thread
   * using async dispatch. If we adopted a similar approach: when an API call was
   * handled on the main thread, rather than calling GetContext() and WriteEvent()
   * synchronously, we'd instead enqueue a callback to be invoked on a background thread
   * once a context and writer were ready.
   *
   * If we want to guarantee thread safety in this SDK, then using async dispatch may be
   * a sensible choice - it would presumably ensure predictable main-thread performance
   * overhead, at the cost of additional complexity, additional copying between threads,
   * and raw speed in idealized single-threaded usage.
   *
   * Until we can profile and evaluate the tradeoffs, though, this straightforward,
   * synchronous approach prevails.
   */
  CoreContext GetContext() const;

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
   * Enqueues an arbitrary event payload to be written to disk in the storage thread.
   *
   * NOTE: Given that most features inevitably need thread-safe access to the
   * CoreContext, it may be worthwhile to use async dispatch (see above).
   */
  bool WriteEvent(Block event, Block event_metadata) const;

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
