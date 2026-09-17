// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <optional>
#include <string_view>

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_types/rum.hpp"

#include "dd-win-prof.h"

namespace datadog::impl {

class Profiling final : public Feature {
 public:
  explicit Profiling(const ProfilerConfig* config);

  FeatureId GetId() const override;
  std::string_view GetName() const override;

  bool Initialize();

  std::optional<Report> UploadThread_PrepareReport(
      BatchReader& reader, HttpRequestBuilder& builder
  ) override;

  std::optional<std::function<void(const FeatureMessage&)>>
  MakeMessageHandler() override;

 protected:
  void Start() override;
  void Stop() override;

 private:
  void OnRumSessionChanged(const RumSessionState& session);
  void OnRumViewChanged(const RumViewEvent& view);
  void OnRumViewLost();

  const ProfilerConfig* _config;
  bool _profiler_setup{false};
  bool _profiler_started{false};
};

}  // namespace datadog::impl
