// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/profiling.hpp"

#include "datadog/core.hpp"

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/profiling/profiler.hpp"
#include "datadog/impl/profiling/profiler_ownership.hpp"
#include "datadog/impl/profiling/profiling.hpp"

namespace datadog {

ProfilingConfig::ProfilingConfig() = default;

ProfilingConfig::ProfilingConfig(const ProfilingConfig&) = default;
ProfilingConfig& ProfilingConfig::operator=(const ProfilingConfig&) = default;
ProfilingConfig::ProfilingConfig(ProfilingConfig&&) noexcept = default;
ProfilingConfig& ProfilingConfig::operator=(ProfilingConfig&&) noexcept = default;

Profiling::Profiling(Profiling::PrivateCtorTag)
    : _impl(nullptr),
      _diagnostic_handler(nullptr),
      _diagnostic_threshold(DiagnosticLevel::Error) {}

Profiling::Profiling(
    std::shared_ptr<impl::Profiling>&& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    Profiling::PrivateCtorTag
)
    : _impl(std::move(impl)),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {
  // The C++ profiling API doesn't currently emit any diagnostic messages, but storing
  // these values ensures that we can do so in the future without ABI changes
  (void)_diagnostic_handler;
  (void)_diagnostic_threshold;
}

Profiling::~Profiling() = default;

std::shared_ptr<Profiling> Profiling::Register(
    const std::shared_ptr<Core>& core, const ProfilingConfig& config
) {
  (void)config;

  // Return a no-op Profiling interface if called without a valid core
  if (!core || !core->_impl) {
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  const impl::DiagnosticLogger diagnostic_logger{
      core->_diagnostic_handler, core->_diagnostic_threshold
  };

  // If this build has no profiler, return a no-op Profiling interface
  auto profiler = impl::CreateProfiler();
  if (!profiler) {
    diagnostic_logger.Warning(
        "Profiling is not supported in this build of the SDK: it requires Windows x64 "
        "and the DD_ENABLE_PROFILING CMake option"
    );
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  // The profiler is process-global, so only one SDK instance may use it at a time: if
  // another already has, return a no-op Profiling interface
  auto ownership = impl::ProfilerOwnership::TryAcquire();
  if (!ownership) {
    diagnostic_logger.Warning(
        "Profiling is already enabled for another SDK instance in this process: the "
        "profiler is shared by the whole process, so only one instance can use it"
    );
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  const impl::ImmutableContext& im = core->_impl->GetImmutableContext();
  impl::ProfilerSettings settings;
  settings.service = im.service;
  settings.env = im.env;
  settings.version = im.application_version;

  auto profiling_impl = std::make_shared<impl::Profiling>(
      std::move(profiler), std::move(settings), std::move(ownership)
  );

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->_impl->RegisterFeature(profiling_impl)) {
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  return std::make_shared<Profiling>(
      std::move(profiling_impl),
      core->_diagnostic_handler,
      core->_diagnostic_threshold,
      Profiling::PrivateCtorTag{}
  );
}

}  // namespace datadog
