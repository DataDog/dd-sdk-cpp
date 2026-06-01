// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <optional>

#include "datadog.hpp"

#include "repl/commands.hpp"
#include "repl/state.hpp"

namespace {

std::optional<datadog::TrackingConsent> ParseTrackingConsent(std::string_view s) {
  if (s == "granted") {
    return datadog::TrackingConsent::Granted;
  }
  if (s == "not-granted") {
    return datadog::TrackingConsent::NotGranted;
  }
  if (s == "pending") {
    return datadog::TrackingConsent::Pending;
  }
  return std::nullopt;
}

}  // namespace

CommandResult HandleCreateCore(State& state, const CommandInput& args) {
  // Default to pending tracking consent, but allow it to be overridden with
  // 'create-core tracking-consent:granted' etc.
  datadog::TrackingConsent tracking_consent = datadog::TrackingConsent::Pending;
  auto named = args.Named();
  if (named.Has("tracking-consent")) {
    if (auto value = ParseTrackingConsent(named.Get("tracking-consent"))) {
      tracking_consent = *value;
    } else {
      return CommandResult::Error("Invalid tracking consent value!");
    }
  }

  if (state.core) {
    return CommandResult::Error("Core already exists!");
  }
  state.core = datadog::Core::Create(state.config, tracking_consent);
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
  std::string_view value_str = args.Peek();
  if (value_str.empty()) {
    return CommandResult::Error("No value supplied for tracking consent!");
  }
  std::optional<datadog::TrackingConsent> value = ParseTrackingConsent(value_str);
  if (!value.has_value()) {
    return CommandResult::Error("Invalid tracking consent value!");
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
    extra.SetObjectProperty(
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
        named.values[i].name,
        datadog::Attribute::String(named.values[i].value)
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    );
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

CommandResult HandleSetAccountInfo(State& state, const CommandInput& args) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }

  // Positional args: id (required)
  auto pos = args.Positional();
  auto id = Unquote(pos[0]);
  if (id.empty()) {
    return CommandResult::Error("No account id given!");
  }

  // Named args: name (optional)
  auto named = args.Named();
  auto name = Unquote(named.Get("name"));

  state.core->SetAccountInfo(id, name);
  return CommandResult::OK("Core::SetAccountInfo()");
}

CommandResult HandleAddAccountExtraInfo(State& state, const CommandInput& args) {
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
    extra.SetObjectProperty(
        // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index)
        named.values[i].name,
        datadog::Attribute::String(named.values[i].value)
        // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
    );
  }

  state.core->AddAccountExtraInfo(extra);
  return CommandResult::OK("Core::AddAccountExtraInfo()");
}

CommandResult HandleClearAccountInfo(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  state.core->ClearAccountInfo();
  return CommandResult::OK("Core::ClearAccountInfo()");
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
