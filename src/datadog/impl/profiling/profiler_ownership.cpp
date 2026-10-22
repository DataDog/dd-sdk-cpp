// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiler_ownership.hpp"

#include <atomic>

namespace datadog::impl {

namespace {

// Whether a ProfilerOwnership currently exists: process-global, like the profiler
std::atomic<bool>& ClaimFlag() {
  static std::atomic<bool> s_claimed{false};
  return s_claimed;
}

}  // namespace

ProfilerOwnership::ProfilerOwnership(PrivateCtorTag) {}

ProfilerOwnership::~ProfilerOwnership() { ClaimFlag().store(false); }

std::unique_ptr<ProfilerOwnership> ProfilerOwnership::TryAcquire() {
  bool expected = false;
  if (!ClaimFlag().compare_exchange_strong(expected, true)) {
    return nullptr;
  }
  return std::make_unique<ProfilerOwnership>(PrivateCtorTag{});
}

}  // namespace datadog::impl
