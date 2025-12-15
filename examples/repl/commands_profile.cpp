// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <charconv>
#include <iostream>
#include <thread>

#include "repl/alloc.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleStartProfile(State& state, const CommandInput& args) {
  if (state.is_profiling) {
    return CommandResult::Error("Already profiling!");
  }

  // Parse mode, defaulting to 'cpu'
  std::string_view mode_str = Unquote(args[0]);
  if (mode_str.empty()) {
    mode_str = "cpu";
  }
  if (mode_str != "cpu" && mode_str != "memory") {
    return CommandResult::Error("Invalid profiling mode!");
  }

  // Enable allocation tracking if profiling in memory mode, assuming the repl binary
  // was built with WITH_DATADOG_ALLOCATION_TRACKING=1
  if (mode_str == "memory") {
#if WITH_DATADOG_ALLOCATION_TRACKING
    StartAllocationTracking();
    state.is_profiling_allocations = true;
#else
    return CommandResult::Error("repl not built WITH_DATADOG_ALLOCATION_TRACKING!");
#endif
  }

  // Echo the hash value for our main thread ID
  std::hash<std::thread::id> hasher;
  const size_t tid = hasher(std::this_thread::get_id());
  std::cout << "<<<(main-thread-id): " << tid << "\n";

  state.is_profiling = true;
  return CommandResult::OK("Profiling started.");
}

CommandResult HandleStopProfile(State& state, const CommandInput&) {
  if (!state.is_profiling) {
    return CommandResult::Error("Not currently profiling!");
  }
  state.is_profiling = false;

#if WITH_DATADOG_ALLOCATION_TRACKING
  if (state.is_profiling_allocations) {
    state.is_profiling_allocations = false;
    const size_t num_events_dropped = StopAllocationTracking();
    if (num_events_dropped > 0) {
      return CommandResult::Error("Profiling results truncated!");
    }
  }
#endif
  return CommandResult::OK("Profiling stopped.");
}
