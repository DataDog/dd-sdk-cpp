// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <memory>

namespace datadog::impl {

/**
 * Claim on the process-global profiler.
 *
 * The profiler is shared by the whole process, not by any one SDK instance (`Core`), so
 * only one `Profiling` feature may drive it at a time: otherwise, instances would
 * overwrite each other's settings, restart a running profiler, or stop profiling while
 * another instance is still using it.
 *
 * At most one `ProfilerOwnership` exists at any moment. The owning `Profiling` feature
 * holds it for its whole lifetime, so the claim is released when the feature is
 * destroyed, allowing a later SDK instance to take over.
 */
class ProfilerOwnership {
 private:
  struct PrivateCtorTag {};

 public:
  // Callers should use ProfilerOwnership::TryAcquire
  explicit ProfilerOwnership(PrivateCtorTag);
  ~ProfilerOwnership();

  // A claim is never copied or moved
  ProfilerOwnership(const ProfilerOwnership&) = delete;
  ProfilerOwnership& operator=(const ProfilerOwnership&) = delete;
  ProfilerOwnership(ProfilerOwnership&&) = delete;
  ProfilerOwnership& operator=(ProfilerOwnership&&) = delete;

  /**
   * Claims the process-global profiler. Returns null if another claim is already held.
   * Thread-safe.
   */
  static std::unique_ptr<ProfilerOwnership> TryAcquire();
};

}  // namespace datadog::impl
