// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "rum.hpp"

namespace datadog::impl {

Rum::Rum(
    const platform::IClock& clock, std::string_view service_name,
    std::string_view application_version
)
    : _clock(clock),
      _service_name(service_name),
      _application_version(application_version) {
  // Constructor accepts config parameters but doesn't use them yet
  // This is a "do-nothing" API implementation
  (void)_clock;  // Suppress unused field warning for future expansion
  (void)_service_name;
  (void)_application_version;
}

std::optional<Report> Rum::UploadThread_PrepareReport(
    const CoreContext& context, BatchReader& reader
) {
  (void)context;
  (void)reader;
  // No events to process yet - return nullopt to indicate no report
  return std::nullopt;
}

}  // namespace datadog::impl
