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

CommandResult HandleSetUserInfo(State& state, const CommandInput& args) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }

  // Positional args: id (required)
  auto pos = args.Positional();
  auto id = Unquote(pos[0]);
  if (id.empty()) {
    return CommandResult::Error("No user id given!");
  }

  // Named args: name, email (optional)
  auto named = args.Named();
  auto name = Unquote(named.Get("name"));
  auto email = Unquote(named.Get("email"));

  state.core->SetUserInfo(id, name, email);
  return CommandResult::OK("Core::SetUserInfo()");
}

CommandResult HandleAddUserExtraInfo(State& state, const CommandInput& args) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }

  // Named args: arbitrary key-value pairs added as string attributes
  auto named = args.Named();
  if (named.n == 0) {
    return CommandResult::Error("No key-value pairs given!");
  }

  datadog::Attribute extra = datadog::Attribute::Object(named.n);
  for (size_t i = 0; i < named.n; i++) {
    extra.SetObjectProperty(named.values[i].name, datadog::Attribute::String(named.values[i].value));
  }

  state.core->AddUserExtraInfo(extra);
  return CommandResult::OK("Core::AddUserExtraInfo()");
}

CommandResult HandleClearUserInfo(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  state.core->ClearUserInfo();
  return CommandResult::OK("Core::ClearUserInfo()");
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
