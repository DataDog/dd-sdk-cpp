// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <optional>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#endif

#include "datadog.hpp"

#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleRegisterCrashReporting(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  if (state.crash_reporting) {
    return CommandResult::Error("Crash Reporting already registered!");
  }
  state.crash_reporting =
      datadog::CrashReporting::Register(state.core, state.crash_reporting_config);
  if (!state.crash_reporting) {
    return CommandResult::Error("CrashReporting::Register() failed!");
  }
  return CommandResult::OK("CrashReporting::Register()");
}

CommandResult HandleCrash(State&, const CommandInput&) {
#ifdef _WIN32
  // Record an access violation: if the SDK has installed a vectored exception handler,
  // the system's SEH handler will call it, allowing the crash to be handled before the
  // process terminates
  RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#else
  // Deliver SIGSEGV to this process: if the SDK has registered a signal handler, the
  // kernel will call it, allowing the crash to be handled before the process terminates
  raise(SIGSEGV);
#endif

  // This code is unreachable if we crash as intended; there can be no successful result
  return CommandResult::Error("HandleCrash did not crash!");
}
