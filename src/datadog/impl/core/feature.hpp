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
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/tlv.hpp"
#include "datadog/impl/platform/filesystem.hpp"
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
 * - In those functions, call `WriteEvent()` for each event the feature generate, using
 *    whatever binary format is appropriate for the feature
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
   * first point at which events may be generated.
   */
  virtual void Start() {}

  /**
   * Called from the main thread when the SDK is about to shut down. This is the last
   * point at which events may be generated.
   */
  virtual void Stop() {}

  /**
   * Callable from the main thread in order to produce a new event. Copies the given
   * event payload(s) into the storage queue so that the storage thread can write them
   * to persistent storage. Once the batch containing this event is ready for upload,
   * the upload thread will pass it to UploadThread_PrepareReport().
   *
   * @returns whether the event was successfully enqueued for storage. If called before
   *  Start() or after Stop(), always returns false.
   */
  bool WriteEvent(Block event, Block event_metadata = {}) const;

  /**
   * @returns whether the feature has received a call to Start() and has not yet
   *  received a call to Stop().
   */
  bool IsRunning() const;

 public:
  /**
   * Called from the upload thread when a batch of events written to storage by this
   * feature is ready to be processed and uploaded.
   */
  virtual std::optional<Report> UploadThread_PrepareReport(
      const HttpContext& context, class BatchReader& reader
  ) = 0;

 protected:
  std::optional<FeatureScope> _scope;
};

}  // namespace datadog::impl
