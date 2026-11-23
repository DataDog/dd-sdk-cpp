// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <memory>
#include <shared_mutex>
#include <string_view>
#include <vector>

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/logging/types.hpp"

namespace datadog::impl {

/**
 * Logging feature implementation. Keeps track of global state and allows Loggers to be
 * created.
 */
class Logging final : public Feature {
 public:
  explicit Logging(const platform::IClock& clock);

  FeatureId GetId() const override { return CreateFeatureId("LOGS"); }

  std::string_view GetName() const override { return "logs"; }

  std::optional<Report> UploadThread_PrepareReport(
      BatchReader& reader, HttpRequestBuilder& builder
  ) override;

 public:
  /**
   * Adds or updates a global logging attribute, which will be included in log events
   * emitted by all loggers (unless shadowed by logger-level or message-level
   * attributes).
   */
  void AddAttribute(std::string_view name, const Attribute& value);

  /**
   * Removes a global logging attribute, if any exists with the given name.
   */
  void RemoveAttribute(std::string_view name);

  /**
   * Creates a new Logger instance that can be wrapped in an API-layer Logger object,
   * permitting the application to make log calls as desired.
   */
  std::unique_ptr<Logger> CreateLogger(const LoggerConfig& config);

 private:
  // Reference to system clock; used to timestamp events
  const platform::IClock& _clock;

  // Global attributes applied to all log events
  Attribute _global_attributes;
  mutable std::shared_mutex _global_attributes_mutex;

  // Reusable buffer for encoding events; accessed only on the context thread
  std::vector<uint8_t> _encode_buffer;

  // Each impl::Logger maintains a std::weak_ptr<impl::Logging> to access clock, global
  // attributes, and encode buffer when handling log calls
  friend class Logger;

 private:
  /**
   * Obtains a thread-safe copy of the current set of global attributes; used by Logger.
   */
  Attribute SnapshotGlobalAttributes() const;
};

}  // namespace datadog::impl
