// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

namespace {

std::optional<datadog::RumActionType> ParseRumActionType(std::string_view s) {
  if (s == "tap") {
    return datadog::RumActionType::Tap;
  }
  if (s == "click") {
    return datadog::RumActionType::Click;
  }
  if (s == "scroll") {
    return datadog::RumActionType::Scroll;
  }
  if (s == "swipe") {
    return datadog::RumActionType::Swipe;
  }
  if (s == "custom") {
    return datadog::RumActionType::Custom;
  }
  return std::nullopt;
}

}  // namespace

CommandResult HandleRegisterRum(State& state, const CommandInput&) {
  if (!state.core) {
    return CommandResult::Error("Core does not exist!");
  }
  if (state.rum) {
    return CommandResult::Error("Rum already registered!");
  }
  state.rum = datadog::Rum::Register(state.core, state.rum_config);
  if (!state.rum) {
    return CommandResult::Error("Rum::Register() failed!");
  }
  return CommandResult::OK("Rum::Register()");
}

CommandResult HandleStopSession(State& state, const CommandInput&) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  state.rum->StopSession();
  return CommandResult::OK("Rum::StopSession()");
}

CommandResult HandleStartView(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto view_key = Unquote(pos[0]);
  if (view_key.empty()) {
    return CommandResult::Error("No view key given!");
  }

  // Named args
  auto named = args.Named();
  auto view_name = Unquote(named.Get("name"));

  state.rum->StartView(view_key, view_name);
  return CommandResult::OK("Rum::StartView()");
}

CommandResult HandleStopView(State& state, const CommandInput& args) {
  /// RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto view_key = Unquote(pos[0]);
  if (view_key.empty()) {
    return CommandResult::Error("No view key given!");
  }

  state.rum->StopView(view_key);
  return CommandResult::OK("Rum::StopView()");
}

CommandResult HandleAddAction(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto name = Unquote(pos[0]);
  if (name.empty()) {
    return CommandResult::Error("No action name given!");
  }

  // Named args
  auto named = args.Named();
  datadog::RumActionType type{datadog::RumActionType::Custom};
  auto type_str = Unquote(named.Get("type"));
  if (!type_str.empty()) {
    if (auto type_opt = ParseRumActionType(type_str)) {
      type = *type_opt;
    } else {
      return CommandResult::Error("Invalid action type!");
    }
  }

  state.rum->AddAction(type, name);
  return CommandResult::OK("Rum::AddAction()");
}

CommandResult HandleStartAction(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto name = Unquote(pos[0]);
  if (name.empty()) {
    return CommandResult::Error("No action name given!");
  }

  // Named args
  auto named = args.Named();
  datadog::RumActionType type{datadog::RumActionType::Custom};
  auto type_str = Unquote(named.Get("type"));
  if (!type_str.empty()) {
    if (auto type_opt = ParseRumActionType(type_str)) {
      type = *type_opt;
    } else {
      return CommandResult::Error("Invalid action type!");
    }
  }

  state.rum->StartAction(type, name);
  return CommandResult::OK("Rum::StartAction()");
}

CommandResult HandleStopAction(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto name = Unquote(pos[0]);
  if (name.empty()) {
    return CommandResult::Error("No action name given!");
  }

  // Named args
  auto named = args.Named();
  datadog::RumActionType type{datadog::RumActionType::Custom};
  auto type_str = Unquote(named.Get("type"));
  if (!type_str.empty()) {
    if (auto type_opt = ParseRumActionType(type_str)) {
      type = *type_opt;
    } else {
      return CommandResult::Error("Invalid action type!");
    }
  }

  state.rum->StopAction(type, name);
  return CommandResult::OK("Rum::StopAction()");
}
