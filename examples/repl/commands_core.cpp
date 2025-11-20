// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleCreateCore(State& state, const CommandInput&) {
  if (state.core) {
    return CommandResult::Error("Core already exists!");
  }
  state.core = datadog::Core::Create(state.config);
  if (!state.core) {
    return CommandResult::Error("Core::Create() failed!");
  }
  return CommandResult::OK("Core::Create()");
}

CommandResult HandleResetCore(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  state.core.reset();
  return CommandResult::OK("std::shared_ptr<Core>::reset()");
}

CommandResult HandleSetTrackingConsent(State& state, const CommandInput& args) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }

  // args[0] must be 'granted', 'not-granted', or 'pending'
  std::optional<datadog::TrackingConsent> value;
  std::string_view value_str = args.Peek();
  if (value_str.empty()) {
    return CommandResult::Error("No value supplied for tracking consent!");
  }
  if (value_str == "granted") {
    value = datadog::TrackingConsent::Granted;
  } else if (value_str == "not-granted") {
    value = datadog::TrackingConsent::NotGranted;
  } else if (value_str == "pending") {
    value = datadog::TrackingConsent::Pending;
  } else {
    return CommandResult::Error(
        "Invalid tracking consent value! Expected one of granted|not-granted|pending"
    );
  }
  state.core->SetTrackingConsent(*value);
  return CommandResult::OK("Core::SetTrackingConsent()");
}

CommandResult HandleStartCore(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  bool started = state.core->Start();
  if (!started) {
    return CommandResult::Error("Core::Start() failed!");
  }
  state.started = started;
  return CommandResult::OK("Core::Start()");
}

CommandResult HandleStopCore(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  state.core->Stop();
  state.started = false;
  return CommandResult::OK("Core::Stop()");
}
