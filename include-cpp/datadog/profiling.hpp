// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

#include "datadog/api.hpp"
#include "datadog/core.hpp"

// Forward declare dd-win-prof's config struct (defined in dd-win-prof.h)
struct _ProfilerConfig;
typedef struct _ProfilerConfig ProfilerConfig;

namespace datadog {

// Forward declarations
namespace impl {
class Profiling;
}  // namespace impl

/**
 * Interface to the Datadog SDK's profiling feature.
 *
 * Profiling uses the dd-win-prof library to collect CPU and wall-time profiles
 * on Windows. Unlike RUM and Logging, the profiler manages its own upload
 * pipeline via libdatadog.
 *
 * Configuration is done via dd-win-prof's ProfilerConfig struct (from
 * dd-win-prof.h). The SDK does not wrap or duplicate that struct — callers
 * fill it directly and pass a pointer to Register().
 */
class Profiling {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Profiling::Register
  explicit Profiling(PrivateCtorTag);
  explicit Profiling(
      std::shared_ptr<impl::Profiling>&& impl,
      DiagnosticHandler diagnostic_handler,
      DiagnosticLevel diagnostic_threshold,
      PrivateCtorTag
  );
  DATADOG_API ~Profiling();

 public:
  /**
   * Registers the profiling feature with the core of the Datadog SDK.
   *
   * @param core The SDK core instance.
   * @param config Pointer to a ProfilerConfig struct (from dd-win-prof.h).
   *   The struct is read during Register() and does not need to outlive the
   *   call. Pass nullptr to use dd-win-prof's defaults (env vars + built-in
   *   defaults).
   *
   * On Core::Start(), the profiler will be set up and started. On Core::Stop(),
   * the profiler will be stopped.
   */
  DATADOG_API static std::shared_ptr<Profiling> Register(
      const std::shared_ptr<class Core>& core, const ProfilerConfig* config = nullptr
  );

 private:
  // Forbid copying/moving: we use std::shared_ptr<Profiling> at the API boundary
  Profiling(const Profiling&) = delete;
  Profiling& operator=(const Profiling&) = delete;
  Profiling(Profiling&&) = delete;
  Profiling& operator=(Profiling&&) = delete;

  std::shared_ptr<impl::Profiling> _impl;
  DiagnosticHandler _diagnostic_handler;
  DiagnosticLevel _diagnostic_threshold;
};

}  // namespace datadog
