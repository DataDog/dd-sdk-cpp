// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <string_view>

#include "datadog/rum.hpp"

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
  // We store a deep copy of the config so string pointers remain valid.
  // The original ProfilerConfig uses const char* — we own the strings here.
  bool _has_config{false};
  ProfilerConfig _config{};

  // Owned string storage for _config's const char* fields
  std::string _url;
  std::string _api_key;
  std::string _service_environment;
  std::string _service_name;
  std::string _service_version;
  std::string _tags;
  std::string _pprof_output_directory;

  bool _profiler_setup{false};
};

}  // namespace datadog::impl
