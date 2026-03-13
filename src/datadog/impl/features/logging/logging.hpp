// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <shared_mutex>
#include <string>
#include <vector>

#include "datadog/impl/attribute/typed_attribute.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/features/logging/types.hpp"
#include "datadog/impl/platform/clock.hpp"

namespace datadog::impl {

struct LoggerEnrichmentConfig;
struct LoggerState;
struct LogCommandParams;

/**
 * Logging feature implementation. Keeps track of global state, creates loggers, and
 * handles generation of log events in response to logger calls.
 */
class Logging final : public Feature {
 public:
  explicit Logging(
      const platform::IClock& clock,
      std::string_view service_name,
      std::string_view application_version
  );

  FeatureId GetId() const override { return CreateFeatureId("LOGS"); }

  std::string_view GetName() const override { return "logs"; }

  std::optional<Report> UploadThread_PrepareReport(
      const HttpContext& context, BatchReader& reader
  ) override;

 public:
  void AddAttribute(std::string_view name, const Attribute& value);
  void RemoveAttribute(std::string_view name);
  std::unique_ptr<Logger> CreateLogger(const LoggerConfig& config);

 private:
  /**
   * Builds an event payload for a message emitted by a logger, then pushes that event
   * onto the storage queue.
   *
   * @param mut_state Logger-owned state containing attribute values and reusable
   *  attributes/buffers required to build and serialize a log event payload.
   * @param enrichment Configuration details specifying which features, if any, should
   *  have their context injected into the log event for enrichment/correlation.
   * @param level The level at which this log message is being emitted.
   * @param message The text of the log message.
   * @param message_attributes An optional set of message-level attributes, which will
   *  be merged into the event payload if this value has type ValueType::Object. If any
   *  other type, this value will be ignored.
   */
  void OnLoggerEmit(
      struct LoggerState& mut_state,
      const struct LoggerEnrichmentConfig& enrichment,
      LogLevel level,
      std::string_view message,
      const Attribute& message_attributes
  );

  /**
   * Dispatches async log event processing to the context thread via
   * ExecuteOnContextThread. Creates a weak_ptr for shutdown safety.
   */
  void DispatchAsync(const LogCommandParams& params);

  /**
   * Processes a log event on the context thread. Builds the event, enriches it with
   * context data, encodes it to JSON, and writes it via the EventWriter.
   */
  void ProcessLogEvent(
      const LogCommandParams& params,
      const CoreContext& context,
      const EventWriter& writer
  ) const;

 private:
  // Reference to system clock; used to timestamp events
  const platform::IClock& _clock;

  // Immutable global state injected in init
  const std::string _sdk_version;
  const std::string _default_service_name;
  const std::string _application_version;

  // Global attributes applied to all log events
  ObjectAttribute _global_attributes;
  mutable std::shared_mutex _global_attributes_mutex;

  // Reusable buffer for encoding events; accessed only on the context thread
  mutable std::vector<uint8_t> _encode_buffer;

  // HTTP request details used on upload; owned by the upload thread
  std::string _request_url;
  std::string _request_headers;
};

}  // namespace datadog::impl
