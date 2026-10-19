// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>
#include <type_traits>

#include "datadog/api.hpp"
#include "datadog/core.hpp"

namespace datadog {

// Forward declarations
namespace impl {
class Profiling;
}  // namespace impl

/**
 * Configures the Profiling feature.
 *
 * Profiling is only available when the SDK is built for Windows x64 with the CMake
 * option DD_ENABLE_PROFILING=ON. In all other builds, the API remains usable but all
 * calls are no-ops.
 */
struct ProfilingConfig {
  /**
   * Initializes a new Profiling configuration object with default values.
   */
  DATADOG_API ProfilingConfig();

  ~ProfilingConfig() = default;

  // ProfilingConfig is copyable and movable
  DATADOG_API ProfilingConfig(const ProfilingConfig&);
  DATADOG_API ProfilingConfig& operator=(const ProfilingConfig&);
  DATADOG_API ProfilingConfig(ProfilingConfig&&) noexcept;
  DATADOG_API ProfilingConfig& operator=(ProfilingConfig&&) noexcept;
};

// ProfilingConfig has no settings yet. This fires if a non-trivially-destructible
// member is added, as a prompt to revisit the inline `= default` destructor.
static_assert(
    std::is_trivially_destructible_v<ProfilingConfig>,
    "ProfilingConfig must be trivially destructible"
);

/**
 * Interface to the Datadog SDK's Profiling feature.
 *
 * When Core::Start() is called, the feature configures the dd-win-prof profiler with
 * the service, environment, and version provided to the Core, and starts it, but only
 * while tracking consent is TrackingConsent::Granted. dd-win-prof uploads profiles
 * itself, so it cannot hold data back while consent is pending: the profiler does not
 * run at all under TrackingConsent::Pending or TrackingConsent::NotGranted, starts when
 * consent becomes granted, and stops if consent is later withdrawn. It is also stopped
 * when the Core stops. dd-win-prof uses the endpoint settings from its own environment
 * variables.
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
   * Registers the Profiling feature with the core of the Datadog SDK.
   *
   * You MUST call this function after creating the Core and before calling
   * Core::Start().
   */
  DATADOG_API static std::shared_ptr<Profiling> Register(
      const std::shared_ptr<class Core>& core,
      const ProfilingConfig& config = ProfilingConfig()
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
