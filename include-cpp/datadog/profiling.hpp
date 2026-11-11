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
 * When Core::Start() is called, the feature starts profiling the application, and
 * attributes the profiles to the service, environment, and version provided to the
 * Core. Each profile is correlated with the RUM events, such as vitals and long tasks,
 * that occurred while it was collected. A profile with no such events to correlate it
 * with is not kept.
 *
 * Profiling follows the Core's tracking consent. It does not run if consent is
 * TrackingConsent::NotGranted, and stops if consent is later withdrawn. While consent
 * is TrackingConsent::Pending it does run, and what it collects is held, like the data
 * of other features, until consent is granted, or deleted if consent is not granted.
 * Profiling also stops when the Core stops.
 */
class Profiling {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use Profiling::Register
  explicit Profiling(PrivateCtorTag);
  explicit Profiling(
      const std::shared_ptr<impl::Profiling>& impl,
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
   *
   * Only one Core in a process can use Profiling at a time. If another Core has
   * already registered Profiling, this logs a warning and returns an object whose calls
   * do nothing. Profiling becomes available again once that Core is destroyed.
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

  // Held weakly, so that the Core's own reference is the only thing that keeps the
  // feature alive. The feature owns the claim on the process-global profiler: if this
  // handle shared ownership of it, an application that kept the handle after its Core
  // was destroyed would hold the claim, and the profiler would never become available
  // to another Core.
  std::weak_ptr<impl::Profiling> _impl;
  DiagnosticHandler _diagnostic_handler;
  DiagnosticLevel _diagnostic_threshold;
};

}  // namespace datadog
