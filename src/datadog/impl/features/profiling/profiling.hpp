// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <string_view>

#include "datadog/rum.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature.hpp"

#include "dd-win-prof.h"

namespace datadog::impl {

/**
 * Internal implementation of the profiling feature.
 *
 * Wraps the dd-win-prof C API (SetupProfiler, StartProfiler, StopProfiler) and
 * integrates it into the SDK's feature lifecycle. Unlike RUM and Logging, the
 * profiler manages its own data upload via libdatadog, so
 * UploadThread_PrepareReport always returns nullopt.
 */
class Profiling final : public Feature {
 public:
  // config may be nullptr (use dd-win-prof defaults)
  explicit Profiling(const ProfilerConfig* config);

  FeatureId GetId() const override;
  std::string_view GetName() const override;

  std::optional<Report> UploadThread_PrepareReport(
      const HttpContext& context, BatchReader& reader
  ) override;

  /**
   * Forwards RUM context changes to dd-win-prof's SetRumSession()/SetRumView().
   * Converts UUIDs to string representations as expected by the C API.
   */
  void OnRumContextChanged(const datadog::RumContextSnapshot& context);

 protected:
  void Start() override;
  void Stop() override;

 private:
  bool _profiler_setup{false};

  // Change detection for RUM context — avoid redundant calls to dd-win-prof
  datadog::UUID _prev_application_id;
  datadog::UUID _prev_session_id;
  datadog::UUID _prev_view_id;
};

}  // namespace datadog::impl
