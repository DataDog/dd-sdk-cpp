// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/pipeline_factory.hpp"

#include <utility>

#include "datadog/impl/rum/timeseries/buffer.hpp"
#include "datadog/impl/rum/timeseries/cpu_event_factory.hpp"
#include "datadog/impl/rum/timeseries/cpu_reader.hpp"
#include "datadog/impl/types/assert.hpp"

namespace datadog::impl {

namespace {

std::shared_ptr<IPipeline> CreateMemoryPipeline() {
  // TODO(RUM-19588): create the memory pipeline
  return nullptr;
}

}  // namespace

PipelineFactory::PipelineFactory(
    TimeseriesConfig config,
    const platform::IClock& clock,
    std::shared_ptr<const platform::ICpuInfoProvider> cpu_info_provider,
    std::shared_ptr<CollectedTimeseriesSessionRegistry> collected_sessions,
    RunOnContextThread run_on_context_thread,
    DiagnosticLogger logger
)
    : _config(std::move(config)),
      _clock(&clock),
      _cpu_info_provider(std::move(cpu_info_provider)),
      _collected_sessions(std::move(collected_sessions)),
      _run_on_context_thread(std::move(run_on_context_thread)),
      _logger(std::move(logger)) {
  DATADOG_ASSERT(
      _cpu_info_provider != nullptr, "pipeline factory requires a CPU info provider"
  );
}

std::vector<std::shared_ptr<IPipeline>> PipelineFactory::Create(
    const TimeseriesBoundSession& session
) const {
  if (session.session_id == UUID::Zero) {
    _logger.Warning("Not creating timeseries pipelines: session ID is not set");
    return {};
  }

  std::vector<std::shared_ptr<IPipeline>> pipelines;
  pipelines.reserve(_config.types.size());
  for (const TimeseriesType type : _config.types) {
    std::shared_ptr<IPipeline> pipeline;
    switch (type) {
      case TimeseriesType::Cpu:
        pipeline = CreateCpuPipeline(session);
        break;
      case TimeseriesType::Memory:
        pipeline = CreateMemoryPipeline();
        break;
    }
    if (pipeline == nullptr) {
      _logger.Error(
          "Pipeline creation failed", {{"type", TimeseriesType_ToString(type)}}
      );
      continue;
    }
    pipelines.push_back(std::move(pipeline));
  }
  return pipelines;
}

std::shared_ptr<IPipeline> PipelineFactory::CreateCpuPipeline(
    const TimeseriesBoundSession& session
) const {
  // The reader also uses the interval to detect gaps between samples
  return Pipeline<double>::Create(
      session,
      std::make_unique<CpuReader>(
          *_clock, _config.sampling_interval, _cpu_info_provider
      ),
      std::make_unique<CpuEventFactory>(),
      Buffer<double>(_config.buffer_size),
      _collected_sessions,
      _run_on_context_thread,
      _logger
  );
}

}  // namespace datadog::impl
