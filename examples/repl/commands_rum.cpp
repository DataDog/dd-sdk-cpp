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

std::optional<datadog::RumResourceMethod> ParseRumResourceMethod(std::string_view s) {
  if (s == "get") {
    return datadog::RumResourceMethod::Get;
  }
  if (s == "head") {
    return datadog::RumResourceMethod::Head;
  }
  if (s == "post") {
    return datadog::RumResourceMethod::Post;
  }
  if (s == "put") {
    return datadog::RumResourceMethod::Put;
  }
  if (s == "delete") {
    return datadog::RumResourceMethod::Delete;
  }
  if (s == "connect") {
    return datadog::RumResourceMethod::Connect;
  }
  if (s == "options") {
    return datadog::RumResourceMethod::Options;
  }
  if (s == "trace") {
    return datadog::RumResourceMethod::Trace;
  }
  if (s == "patch") {
    return datadog::RumResourceMethod::Patch;
  }
  return std::nullopt;
}

std::optional<datadog::RumResourceType> ParseRumResourceType(std::string_view s) {
  if (s == "unknown") {
    return datadog::RumResourceType::Unknown;
  }
  if (s == "beacon") {
    return datadog::RumResourceType::Beacon;
  }
  if (s == "fetch") {
    return datadog::RumResourceType::Fetch;
  }
  if (s == "xhr") {
    return datadog::RumResourceType::Xhr;
  }
  if (s == "document") {
    return datadog::RumResourceType::Document;
  }
  if (s == "native") {
    return datadog::RumResourceType::Native;
  }
  if (s == "image") {
    return datadog::RumResourceType::Image;
  }
  if (s == "js") {
    return datadog::RumResourceType::Js;
  }
  if (s == "font") {
    return datadog::RumResourceType::Font;
  }
  if (s == "css") {
    return datadog::RumResourceType::Css;
  }
  if (s == "media") {
    return datadog::RumResourceType::Media;
  }
  if (s == "other") {
    return datadog::RumResourceType::Other;
  }
  return std::nullopt;
}

std::optional<datadog::RumErrorSource> ParseRumErrorSource(std::string_view s) {
  if (s == "network") {
    return datadog::RumErrorSource::Network;
  }
  if (s == "source") {
    return datadog::RumErrorSource::Source;
  }
  if (s == "console") {
    return datadog::RumErrorSource::Console;
  }
  if (s == "logger") {
    return datadog::RumErrorSource::Logger;
  }
  if (s == "agent") {
    return datadog::RumErrorSource::Agent;
  }
  if (s == "webview") {
    return datadog::RumErrorSource::Webview;
  }
  if (s == "custom") {
    return datadog::RumErrorSource::Custom;
  }
  if (s == "report") {
    return datadog::RumErrorSource::Report;
  }
  return std::nullopt;
}

std::optional<datadog::RumOperationFailureReason>
ParseRumFailureReason(std::string_view s) {
  if (s == "error") {
    return datadog::RumOperationFailureReason::Error;
  }
  if (s == "abandoned") {
    return datadog::RumOperationFailureReason::Abandoned;
  }
  if (s == "other") {
    return datadog::RumOperationFailureReason::Other;
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

CommandResult HandleStartResource(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto key = Unquote(pos[0]);
  if (key.empty()) {
    return CommandResult::Error("No resource key given!");
  }
  auto url = Unquote(pos[1]);
  if (url.empty()) {
    return CommandResult::Error("No URL given!");
  }

  // Named args
  auto named = args.Named();
  datadog::RumResourceMethod method{datadog::RumResourceMethod::Get};
  auto method_str = Unquote(named.Get("method"));
  if (!method_str.empty()) {
    if (auto method_opt = ParseRumResourceMethod(method_str)) {
      method = *method_opt;
    } else {
      return CommandResult::Error("Invalid resource method!");
    }
  }

  state.rum->StartResource(key, method, url);
  return CommandResult::OK("Rum::StartResource()");
}

CommandResult HandleStopResource(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto key = Unquote(pos[0]);
  if (key.empty()) {
    return CommandResult::Error("No resource key given!");
  }

  // Named args
  auto named = args.Named();

  int32_t status_code{200};
  if (named.Has("status")) {
    status_code = static_cast<int32_t>(named.GetInt("status"));
  }

  int64_t size{-1};
  if (named.Has("size")) {
    size = named.GetInt("size");
  }

  datadog::RumResourceType type{datadog::RumResourceType::Unknown};
  auto type_str = Unquote(named.Get("type"));
  if (!type_str.empty()) {
    if (auto type_opt = ParseRumResourceType(type_str)) {
      type = *type_opt;
    } else {
      return CommandResult::Error("Invalid resource type!");
    }
  }

  state.rum->StopResource(key, status_code, size, type);
  return CommandResult::OK("Rum::StopResource()");
}

CommandResult HandleStopResourceWithError(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto key = Unquote(pos[0]);
  if (key.empty()) {
    return CommandResult::Error("No resource key given!");
  }
  auto error_message = Unquote(pos[1]);
  if (error_message.empty()) {
    return CommandResult::Error("No error message given!");
  }

  // Named args
  auto named = args.Named();

  std::string_view error_type = Unquote(named.Get("type"));
  std::string_view error_stack_trace = Unquote(named.Get("stack"));
  const bool is_network_error = named.GetFlag("network");

  int32_t status_code{200};
  if (named.Has("status")) {
    status_code = static_cast<int32_t>(named.GetInt("status"));
  }

  state.rum->StopResourceWithError(
      key, error_message, error_type, error_stack_trace, is_network_error, status_code
  );
  return CommandResult::OK("Rum::StopResourceWithError()");
}

CommandResult HandleAddError(State& state, const CommandInput& args) {
  // RUM must be registered and SDK must be running
  if (!state.rum) {
    return CommandResult::Error("RUM is not registered!");
  }
  if (!state.started) {
    return CommandResult::Error("SDK is not running!");
  }

  // Positional args
  auto pos = args.Positional();
  auto message = Unquote(pos[0]);
  if (message.empty()) {
    return CommandResult::Error("No error message given!");
  }

  // Named args
  auto named = args.Named();
  std::string_view type = Unquote(named.Get("type"));
  std::string_view stack_trace = Unquote(named.Get("stack"));

  datadog::RumErrorSource source{datadog::RumErrorSource::Source};
  auto source_str = Unquote(named.Get("source"));
  if (!source_str.empty()) {
    if (auto source_opt = ParseRumErrorSource(source_str)) {
      source = *source_opt;
    } else {
      return CommandResult::Error("Invalid error source!");
    }
  }

  state.rum->AddError(source, message, type, stack_trace);
  return CommandResult::OK("Rum::AddError()");
}

CommandResult HandleStartOperation(State& state, const CommandInput& args) {
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
    return CommandResult::Error("No operation name given!");
  }

  // Named args
  auto named = args.Named();
  auto key = Unquote(named.Get("key"));

  state.rum->StartOperation(name, key);
  return CommandResult::OK("Rum::StartOperation()");
}

CommandResult HandleSucceedOperation(State& state, const CommandInput& args) {
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
    return CommandResult::Error("No operation name given!");
  }

  // Named args
  auto named = args.Named();
  auto key = Unquote(named.Get("key"));

  state.rum->SucceedOperation(name, key);
  return CommandResult::OK("Rum::SucceedOperation()");
}

CommandResult HandleFailOperation(State& state, const CommandInput& args) {
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
    return CommandResult::Error("No operation name given!");
  }

  // Named args
  auto named = args.Named();

  datadog::RumOperationFailureReason reason{
      datadog::RumOperationFailureReason::Error};
  auto reason_str = Unquote(named.Get("reason"));
  if (!reason_str.empty()) {
    if (auto reason_opt = ParseRumFailureReason(reason_str)) {
      reason = *reason_opt;
    } else {
      return CommandResult::Error("Invalid failure reason!");
    }
  }

  auto key = Unquote(named.Get("key"));

  state.rum->FailOperation(name, reason, key);
  return CommandResult::OK("Rum::FailOperation()");
}
