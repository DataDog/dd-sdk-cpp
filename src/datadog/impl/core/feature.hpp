// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "nonstd/expected.hpp"

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_id.hpp"
#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/tlv.hpp"
#include "datadog/impl/platform/http.hpp"

namespace datadog::impl {

/**
 * Lightweight description of an HTTP request that should be made in order to upload a
 * batch of event data for a specific feature.
 */
struct Report {
  /**
   * Fully qualified URL (including query parameters if applicable) to use for the
   * request. All requests are assumed to be POSTs. Underlying string data must persist
   * throughout the lifetime of the HTTP request.
   */
  const std::string& url;
  /**
   * The set of headers to use with this request, in wire format (e.g. 'Foo: bar'),
   * newline-delimited, with a trailing newline.
   */
  const std::string& headers;
  /**
   * A function that will populate the body of the HTTP request on demand, allowing
   * large event payloads to be streamed directly to the HTTP layer without intermediate
   * copies.
   */
  platform::HttpBodyWriter body_writer;
};

/**
 * Describes how the storage thread should enforce size limits and other constraints on
 * the batch files for a specific feature.
 */
struct FeatureStorageConfig {
  // TODO: Implement defaults, use these values in BatchWriter
};

/**
 * Base class used for the implementation of a feature. Implements user-facing API
 * operations, generates event payloads for storage, and processes batches of those
 * events for periodic upload.
 *
 * The Core is only aware of the Feature interface, and it keeps track of registered
 * features via its own feature-agnostic data stucture, RegisteredFeature.
 *
 * To implement a new feature:
 *
 * - Establish a new source module `src/datadog/impl/features/foo`
 * - In that module, define `class Foo final : public Feature { ... };`
 * - Implement `GetId()` to return the globally unique FourCC code for that feature
 * - Implement `GetName()` to return the globally unique name for logs, storage, etc.
 * - If the feature has unique storage requirements, implement `GetStorageConfig()`
 * - If the feature needs initialization/shutdown logic, implement `Start()`/`Stop()`
 * - Define main-thread-callable functions for the required feature-specific operations
 * - In those functions, call `_scope->ExecuteOnContextThread()` to generate events
 *    asynchronously on the context thread, using the provided `EventWriter`
 * - Implement `UploadThread_PrepareReport()` to read batches of events (in the same
 *    format) and return a `Report` object describing the resulting HTTP request that
 *    should be made to upload that batch to the appropriate intake endpoint
 *
 * Additionally, for the user-facing operations that your new feature exposes:
 *
 * - Create `include-c/datadog/foo.h` and declare the feature's C API
 * - Add `#include "datadog/foo.h"` to `include-c/datadog.h`
 * - Create `src/datadog/c/foo.cpp` with bindings to the underlying implementation.
 * - Create `include-cpp/datadog/foo.hpp` and declare the feature's C++ API
 * - Add `#include "datadog/foo.hpp"` to `include-cpp/datadog.hpp`
 * - Create `src/datadog/cpp/foo.cpp` with bindings to the underlying implementation.
 */
class Feature : public std::enable_shared_from_this<Feature> {
 public:
  Feature() = default;
  virtual ~Feature() = default;

  // Noncopyable, movable
  Feature(const Feature&) = delete;
  Feature& operator=(const Feature&) = delete;
  Feature(Feature&&) = default;
  Feature& operator=(Feature&&) = default;

  virtual FeatureId GetId() const = 0;
  virtual std::string_view GetName() const = 0;
  virtual FeatureStorageConfig GetStorageConfig() const { return {}; }

  void OnCoreStarted(FeatureScope&& feature_scope);
  void OnCoreStopping();

 protected:
  /**
   * Called from the main thread when the SDK has finished starting up. This is the
   * first point at which _scope is valid.
   *
   * Once Start() is called, the feature may enqueue work on the context thread via
   * _scope->ExecuteOnContextThread(), generating events in the process, until Stop() is
   * called.
   */
  virtual void Start() {}

  /**
   * Called from the main thread when the SDK is shutting down. This is the last point
   * at which _scope is valid.
   *
   * Once Stop() is called, any work enqueued via _scope->ExecuteOnContextThread() will
   * be ignored, and the feature may no longer generate events.
   */
  virtual void Stop() {}

 public:
  /**
   * Called from the upload thread when a batch of events written to storage by this
   * feature is ready to be processed and uploaded.
   */
  virtual std::optional<Report> UploadThread_PrepareReport(
      const HttpContext& context, class BatchReader& reader
  ) = 0;

  /**
   * Called by `Core::Start()` each time the SDK starts up. If the feature wants to
   * react to messages dispatched on the `MessageBus`, it should override this method
   * and return a handler function: typically a lambda that captures `weak_from_this()`
   * so that message delivery is silently skipped if the feature has already been
   * destroyed.
   *
   * Implementations must be idempotent, as this function called on every `Start()`.
   *
   * The default implementation returns `std::nullopt`, meaning the feature opts out of
   * message handling entirely.
   */
  virtual std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() {
    return std::nullopt;
  }

 protected:
  std::optional<FeatureScope> _scope;
};

}  // namespace datadog::impl
