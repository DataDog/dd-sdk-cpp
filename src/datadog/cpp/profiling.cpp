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
#include "datadog/impl/profiling/profiling.hpp"

namespace datadog {

ProfilingConfig::ProfilingConfig() = default;

ProfilingConfig::ProfilingConfig(const ProfilingConfig&) = default;
ProfilingConfig& ProfilingConfig::operator=(const ProfilingConfig&) = default;
ProfilingConfig::ProfilingConfig(ProfilingConfig&&) noexcept = default;
ProfilingConfig& ProfilingConfig::operator=(ProfilingConfig&&) noexcept = default;

Profiling::Profiling(Profiling::PrivateCtorTag)
    : _diagnostic_handler(nullptr), _diagnostic_threshold(DiagnosticLevel::Error) {}

Profiling::Profiling(
    const std::shared_ptr<impl::Profiling>& impl,
    DiagnosticHandler diagnostic_handler,
    DiagnosticLevel diagnostic_threshold,
    Profiling::PrivateCtorTag
)
    : _impl(impl),
      _diagnostic_handler(std::move(diagnostic_handler)),
      _diagnostic_threshold(diagnostic_threshold) {
  // This object doesn't currently emit diagnostic messages itself (Profiling::Register
  // logs through the Core's handler), but storing these values ensures that we can do
  // so in the future without ABI changes
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

  // The profiler is process-global: this returns null (after logging why) if this
  // build has no profiler, or if another SDK instance is already using it. In that
  // case, return a no-op Profiling interface
  auto profiler = impl::CreateProfiler(diagnostic_logger);
  if (!profiler) {
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  const impl::ImmutableContext& im = core->_impl->GetImmutableContext();
  impl::ProfilerSettings settings;
  settings.service = im.service;
  settings.env = im.env;
  settings.version = im.application_version;

  auto profiling_impl =
      std::make_shared<impl::Profiling>(std::move(profiler), std::move(settings));

  // Register the feature with the core, returning a no-op interface on failure
  if (!core->_impl->RegisterFeature(profiling_impl)) {
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  return std::make_shared<Profiling>(
      profiling_impl,
      core->_diagnostic_handler,
      core->_diagnostic_threshold,
      Profiling::PrivateCtorTag{}
  );
}

}  // namespace datadog
