#pragma once

#include <cinttypes>
#include <shared_mutex>
#include <string>
#include <vector>

#include "attribute/typed_attribute.hpp"
#include "core/feature.hpp"
#include "features/logging/types.hpp"
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * Logging feature implementation. Keeps track of global state, creates loggers, and
 * handles generation of log events in response to logger calls.
 */
class Logging final : public Feature {
 public:
  explicit Logging(
      const platform::IClock& clock, std::string_view service_name,
      std::string_view application_version
  );

  FeatureId GetId() const override { return CreateFeatureId("LOGS"); }

  std::string_view GetName() const override { return "logs"; }

  std::optional<Report> UploadThread_PrepareReport(
      const CoreContext& context, BatchReader& reader
  ) override;

 public:
  void SetAttribute(std::string_view name, const Attribute& value);
  void DeleteAttribute(std::string_view name);
  std::unique_ptr<Logger> CreateLogger(const LoggerConfig& config);

 private:
  /**
   * Builds an event payload for a message emitted by a logger, then pushes that event
   * onto the storage queue.
   *
   * @param mut_event_object The reusable object Attribute, owned by the logger, in
   *  which the event payload will be constructed (with top-level properties like
   * 'status', 'service', 'message', etc.)
   * @param mut_event_buffer The reusable buffer, owned by the logger, into which that
   *  event payload will be serialized to JSON.
   * @param logger_service_name The string value held by the logger to indicate an
   *  overridden service name; or null if the logger uses the default service name.
   * @param logger_object An object attribute describing the details of the logger.
   * @param level The level at which this log message is being emitted.
   * @param message The text of the log message.
   * @param message_attributes An optional set of message-level attributes, which will
   *  be merged into the event payload if this value has type ValueType::Object. If any
   *  other type, this value will be ignored.
   */
  void OnLoggerEmit(
      Attribute& mut_event_object, std::vector<uint8_t>& mut_event_buffer,
      const StringAttribute& logger_service_name, const ObjectAttribute& logger_object,
      const Attribute& logger_attributes, LogLevel level, std::string_view message,
      const Attribute& message_attributes
  ) const;

  static Attribute GetLogLevelString(LogLevel level);

 private:
  // Reference to system clock; used to timestamp events
  const platform::IClock& _clock;

  // Immutable global state injected in init
  const StringAttribute _sdk_version;
  const StringAttribute _default_service_name;
  const StringAttribute _application_version;

  // Global attributes applied to all log events
  ObjectAttribute _global_attributes;
  mutable std::shared_mutex _global_attributes_mutex;

  // HTTP request details used on upload; owned by the upload thread
  int32_t _last_context_version{0};
  std::string _request_url;
  std::string _request_headers;
};

}  // namespace datadog::impl
