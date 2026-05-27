// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/logging/logging.hpp"

#include "datadog/impl/core/request_builder.hpp"
#include "datadog/impl/core/writer.hpp"
#include "datadog/impl/logging/logger.hpp"

namespace datadog::impl {

Logging::Logging(const platform::IClock& clock)
    : _clock(clock), _global_attributes(8) {}

std::optional<Report> Logging::UploadThread_PrepareReport(
    BatchReader& reader, RequestBuilder& builder
) {
  // Log events are sent to logs intake, as a JSON-serialized array of LogEvent objects
  builder.Reset("/api/v2/logs", "application/json");
  builder.AddQueryParam_ddsource();

  // Prepare a TLVBatchWriter which will stream the contents of the batch file, treating
  // each event block as a JSON object and concatenating those values into a JSON array
  // on the fly as the request body is built
  return Report{builder.GetUrl(), builder.GetHeaders(), TLVBatchWriter{reader}};
}

void Logging::AddAttribute(std::string_view name, const Attribute& value) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.SetObjectProperty(name, value);
}

void Logging::RemoveAttribute(std::string_view name) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.DeleteObjectProperty(name);
}

std::unique_ptr<Logger> Logging::CreateLogger(const LoggerConfig& config) {
  std::weak_ptr<Logging> self = std::static_pointer_cast<Logging>(shared_from_this());
  return std::make_unique<Logger>(config, self);
}

Attribute Logging::SnapshotGlobalAttributes() const {
  std::shared_lock read_lock(_global_attributes_mutex);
  return _global_attributes.attribute;
}

}  // namespace datadog::impl
