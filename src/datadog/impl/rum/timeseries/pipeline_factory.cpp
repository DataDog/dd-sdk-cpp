// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/pipeline_factory.hpp"

#include <utility>

namespace datadog::impl {

namespace {

std::shared_ptr<IPipeline> CreateCpuPipeline() {
  // TODO(RUM-19589): create the CPU pipeline
  return nullptr;
}

std::shared_ptr<IPipeline> CreateMemoryPipeline() {
  // TODO(RUM-19588): create the memory pipeline
  return nullptr;
}

}  // namespace

PipelineFactory::PipelineFactory(
    TimeseriesConfig config,
    const platform::IClock& /*clock*/,
    std::shared_ptr<CollectedTimeseriesSessionRegistry> collected_sessions,
    RunOnContextThread run_on_context_thread,
    DiagnosticLogger logger
)
    : _config(std::move(config)),
      _collected_sessions(std::move(collected_sessions)),
      _run_on_context_thread(std::move(run_on_context_thread)),
      _logger(std::move(logger)) {}

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
        pipeline = CreateCpuPipeline();
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

}  // namespace datadog::impl
