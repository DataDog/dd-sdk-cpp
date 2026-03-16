// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/profiling.hpp"

#include "datadog/core.hpp"

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/features/profiling/profiling.hpp"

namespace datadog {

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
      _diagnostic_threshold(diagnostic_threshold) {}

Profiling::~Profiling() = default;

std::shared_ptr<Profiling> Profiling::Register(
    const std::shared_ptr<Core>& core, const ProfilerConfig* config
) {
  // Return a no-op Profiling interface if called without a valid core
  if (!core || !core->_impl) {
    return std::make_shared<Profiling>(Profiling::PrivateCtorTag{});
  }

  // Initialize our profiling feature implementation (deep-copies config)
  auto profiling_impl = std::make_shared<impl::Profiling>(config);

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
