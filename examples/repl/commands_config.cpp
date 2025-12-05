// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog.hpp"
#include "repl/commands.hpp"
#include "repl/state.hpp"

CommandResult HandleSetConfig(State& state, const CommandInput& args) {
  // set-config client-token <value>
  if (args.Peek() == "client-token") {
    std::string_view value = args[1];
    if (value.empty()) {
      return CommandResult::Error("No value supplied for client-token!");
    }
    state.config.SetClientToken(value);
    state.has_client_token = true;
    return CommandResult::OK("CoreConfig::SetClientToken()");
  }

  // set-config service <value>
  if (args.Peek() == "service") {
    std::string_view value = args[1];
    if (value.empty()) {
      return CommandResult::Error("No value supplied for service!");
    }
    state.config.SetService(value);
    return CommandResult::OK("CoreConfig::SetService()");
  }

  // set-config env <value>
  if (args.Peek() == "env") {
    std::string_view value = args[1];
    if (value.empty()) {
      return CommandResult::Error("No value supplied for env!");
    }
    state.config.SetEnv(value);
    return CommandResult::OK("CoreConfig::SetEnv()");
  }

  // set-config custom-endpoint-url <value>
  if (args.Peek() == "custom-endpoint-url") {
    std::string_view value = args[1];
    if (value.empty()) {
      return CommandResult::Error("No value supplied for custom-endpoint-url!");
    }
    state.config.Internal_UseCustomEndpoint(value);
    return CommandResult::OK("CoreConfig::Internal_UseCustomEndpoint()");
  }

  // set-config flush-on-stop
  if (args.Peek() == "flush-on-stop") {
    state.config.Internal_FlushHttpRequestsOnStop();
    return CommandResult::OK("CoreConfig::Internal_FlushHttpRequestsOnStop()");
  }

  // set-config rum-application-id <value>
  if (args.Peek() == "rum-application-id") {
    std::string_view value = args[1];
    if (value.empty()) {
      return CommandResult::Error("No value supplied for rum-application-id!");
    }
    state.rum_config.SetApplicationId(value);
    state.has_rum_application_id = true;
    if (auto parsed = datadog::UUID::Parse(value)) {
      state.application_id = *parsed;
    }
    return CommandResult::OK("RumConfig::SetApplicationId()");
  }

  return CommandResult::Error("NYI");
}
