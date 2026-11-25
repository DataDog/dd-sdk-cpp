// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog.hpp"

#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleRegisterProfiling(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  if (state.profiling) {
    return CommandResult::Error("Profiling already registered!");
  }

  // In a build without a profiler (anything but Windows x64 with DD_ENABLE_PROFILING),
  // this returns an object whose calls do nothing, after a status message saying why
  state.profiling = datadog::Profiling::Register(state.core);
  if (!state.profiling) {
    return CommandResult::Error("Profiling::Register() failed!");
  }
  return CommandResult::OK("Profiling::Register()");
}
