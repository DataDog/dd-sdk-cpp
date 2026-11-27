// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <vector>

#include "datadog/impl/core/platform/clock.hpp"
#include "datadog/impl/core/platform/cpu_info_provider.hpp"
#include "datadog/impl/rum/timeseries/collected_session_registry.hpp"
#include "datadog/impl/rum/timeseries/configuration.hpp"
#include "datadog/impl/rum/timeseries/pipeline.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Creates the pipelines for the timeseries enabled in a TimeseriesConfig.
 */
class PipelineFactory final {
 public:
  /**
   * `clock` stamps the data points; it must outlive the factory and the pipelines it
   * creates. `cpu_info_provider` is shared by the CPU pipelines; must be set.
   */
  PipelineFactory(
      TimeseriesConfig config,
      const platform::IClock& clock,
      std::shared_ptr<const platform::ICpuInfoProvider> cpu_info_provider,
      std::shared_ptr<CollectedTimeseriesSessionRegistry> collected_sessions,
      RunOnContextThread run_on_context_thread,
      DiagnosticLogger logger
  );

  /**
   * Returns at most one pipeline per type in `config.types`, all bound to `session`:
   * every event they write carries that session. A type whose pipeline cannot be
   * created is skipped and logged as an error.
   *
   * Returns an empty vector, logging a warning, if `session.session_id` is
   * UUID::Zero (no session to attach the data to).
   */
  std::vector<std::shared_ptr<IPipeline>> Create(
      const TimeseriesBoundSession& session
  ) const;

 private:
  std::shared_ptr<IPipeline> CreateCpuPipeline(
      const TimeseriesBoundSession& session
  ) const;

  TimeseriesConfig _config;
  const platform::IClock* _clock;
  std::shared_ptr<const platform::ICpuInfoProvider> _cpu_info_provider;
  std::shared_ptr<CollectedTimeseriesSessionRegistry> _collected_sessions;
  RunOnContextThread _run_on_context_thread;
  DiagnosticLogger _logger;
};

}  // namespace datadog::impl
