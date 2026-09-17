// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/api.hpp"
#include "datadog/core.hpp"

struct _ProfilerConfig;
typedef struct _ProfilerConfig ProfilerConfig;

namespace datadog {

namespace impl {
class Profiling;
}  // namespace impl

/**
 * Interface to the Windows native profiling feature.
 *
 * The profiler collects and uploads profiles through dd-win-prof. RUM session and view
 * context is forwarded automatically when both features are registered with one Core.
 */
class Profiling {
 private:
  struct PrivateCtorTag {};

 public:
  explicit Profiling(PrivateCtorTag);
  explicit Profiling(
      std::shared_ptr<impl::Profiling>&& impl,
      DiagnosticHandler diagnostic_handler,
      DiagnosticLevel diagnostic_threshold,
      PrivateCtorTag
  );
  DATADOG_API ~Profiling();

  /**
   * Registers profiling with the Core.
   *
   * The optional config is read during this call and does not need to outlive it. Pass
   * nullptr to use dd-win-prof environment variables and defaults.
   */
  DATADOG_API static std::shared_ptr<Profiling> Register(
      const std::shared_ptr<class Core>& core, const ProfilerConfig* config = nullptr
  );

 private:
  Profiling(const Profiling&) = delete;
  Profiling& operator=(const Profiling&) = delete;
  Profiling(Profiling&&) = delete;
  Profiling& operator=(Profiling&&) = delete;

  std::shared_ptr<impl::Profiling> _impl;
  DiagnosticHandler _diagnostic_handler;
  DiagnosticLevel _diagnostic_threshold;
};

}  // namespace datadog
