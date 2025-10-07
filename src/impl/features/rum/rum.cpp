// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "rum.hpp"

#include <mutex>
#include <shared_mutex>

namespace datadog::impl {

Rum::Rum(const RumConfig& config, const platform::IClock& clock)
    : _config(config), _clock(clock), _global_attributes(8) {
  // TODO(RUM-11368): Use these values when generating view events
  (void)_config;
  (void)_clock;
}

std::optional<Report> Rum::UploadThread_PrepareReport(
    const HttpContext& context, BatchReader& reader
) {
  // TODO(RUM-11368): Implement processing and upload of events once views exist
  (void)context;
  (void)reader;
  return std::nullopt;
}

void Rum::SetAttribute(std::string_view name, const Attribute& value) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.SetObjectProperty(name, value);
}

void Rum::DeleteAttribute(std::string_view name) {
  std::unique_lock exclusive_write_lock(_global_attributes_mutex);
  _global_attributes.attribute.DeleteObjectProperty(name);
}

}  // namespace datadog::impl
