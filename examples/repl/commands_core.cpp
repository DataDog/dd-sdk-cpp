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

  auto attrs = CollectAttributes(named);
  state.core->SetUserInfo(
      id, name, email, attrs.value_or(datadog::Attribute::Object(0))
  );
  return CommandResult::OK("Core::SetUserInfo()");
}

CommandResult HandleAddUserExtraInfo(State& state, const CommandInput& args) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }

  auto named = args.Named();
  auto attrs = CollectAttributes(named);
  if (!attrs) {
    return CommandResult::Error("No attr: key-value pairs given!");
  }

  state.core->AddUserExtraInfo(*attrs);
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

  auto attrs = CollectAttributes(named);
  state.core->SetAccountInfo(id, name, attrs.value_or(datadog::Attribute::Object(0)));
  return CommandResult::OK("Core::SetAccountInfo()");
}

CommandResult HandleAddAccountExtraInfo(State& state, const CommandInput& args) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }

  auto named = args.Named();
  auto attrs = CollectAttributes(named);
  if (!attrs) {
    return CommandResult::Error("No attr: key-value pairs given!");
  }

  state.core->AddAccountExtraInfo(*attrs);
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
