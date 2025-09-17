// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <shared_mutex>
#include <string>
#include <string_view>

#include "core/feature.hpp"
#include "platform/clock.hpp"

namespace datadog::impl {

/**
 * RUM feature implementation. Provides Real User Monitoring functionality
 * for tracking sessions, views, and actions in C++ applications.
 */
class Rum final : public Feature {
 public:
  explicit Rum(
      const platform::IClock& clock, std::string_view service_name,
      std::string_view application_version
  );

  FeatureId GetId() const override { return CreateFeatureId("RUMS"); }

  std::string_view GetName() const override { return "rum"; }

  std::optional<Report> UploadThread_PrepareReport(
      const CoreContext& context, BatchReader& reader
  ) override;

 private:
  // Basic config parameters for future expansion (currently unused)
  const platform::IClock& _clock;
  const std::string _service_name;
  const std::string _application_version;

  // Thread-safe member variables for future state
  mutable std::shared_mutex _state_mutex;
};

}  // namespace datadog::impl
