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

std::optional<datadog::Site> ParseSite(std::string_view s) {
  if (s == "us1") {
    return datadog::Site::us1;
  }
  if (s == "us3") {
    return datadog::Site::us3;
  }
  if (s == "us5") {
    return datadog::Site::us5;
  }
  if (s == "eu1") {
    return datadog::Site::eu1;
  }
  if (s == "ap1") {
    return datadog::Site::ap1;
  }
  if (s == "ap2") {
    return datadog::Site::ap2;
  }
  if (s == "us1_fed" || s == "us1-fed") {
    return datadog::Site::us1_fed;
  }
  return std::nullopt;
}

std::optional<datadog::BatchSize> ParseBatchSize(std::string_view s) {
  if (s == "small") {
    return datadog::BatchSize::Small;
  }
  if (s == "medium") {
    return datadog::BatchSize::Medium;
  }
  if (s == "large") {
    return datadog::BatchSize::Large;
  }
  return std::nullopt;
}

std::optional<datadog::UploadFrequency> ParseUploadFrequency(std::string_view s) {
  if (s == "frequent") {
    return datadog::UploadFrequency::Frequent;
  }
  if (s == "average") {
    return datadog::UploadFrequency::Average;
  }
  if (s == "rare") {
    return datadog::UploadFrequency::Rare;
  }
  return std::nullopt;
}

std::optional<datadog::BatchProcessingLevel> ParseBatchProcessingLevel(
    std::string_view s
) {
  if (s == "low") {
    return datadog::BatchProcessingLevel::Low;
  }
  if (s == "medium") {
    return datadog::BatchProcessingLevel::Medium;
  }
  if (s == "high") {
    return datadog::BatchProcessingLevel::High;
  }
  return std::nullopt;
}

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
CommandResult HandleSetConfig(State& state, const CommandInput& args) {
  // set-config tracking-consent <value>
  if (args.Peek() == "tracking-consent") {
    std::string_view value_str = args[1];
    if (value_str.empty()) {
      return CommandResult::Error("No value supplied for tracking-consent!");
    }
    if (auto value_opt = ParseTrackingConsent(value_str)) {
      state.config.SetInitialTrackingConsent(*value_opt);
      return CommandResult::OK("CoreConfig::SetTrackingConsent()");
    }
    return CommandResult::Error("Invalid tracking consent!");
  }

  // set-config site <value>
  if (args.Peek() == "site") {
    std::string_view value_str = args[1];
    if (value_str.empty()) {
      return CommandResult::Error("No value supplied for site!");
    }
    if (auto value_opt = ParseSite(value_str)) {
      state.config.SetSite(*value_opt);
      return CommandResult::OK("CoreConfig::SetSite()");
    }
    return CommandResult::Error("Invalid site!");
  }

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

  // set-config version <value>
  if (args.Peek() == "version") {
    std::string_view value = args[1];
    if (value.empty()) {
      return CommandResult::Error("No value supplied for version!");
    }
    state.config.SetApplicationVersion(value);
    return CommandResult::OK("CoreConfig::SetApplicationVersion()");
  }

  // set-config batch-size <value>
  if (args.Peek() == "batch-size") {
    std::string_view value_str = args[1];
    if (value_str.empty()) {
      return CommandResult::Error("No value supplied for batch-size!");
    }
    if (auto value_opt = ParseBatchSize(value_str)) {
      state.config.SetBatchSize(*value_opt);
      return CommandResult::OK("CoreConfig::SetBatchSize()");
    }
    return CommandResult::Error("Invalid batch size!");
  }

  // set-config upload-frequency <value>
  if (args.Peek() == "upload-frequency") {
    std::string_view value_str = args[1];
    if (value_str.empty()) {
      return CommandResult::Error("No value supplied for upload-frequency!");
    }
    if (auto value_opt = ParseUploadFrequency(value_str)) {
      state.config.SetUploadFrequency(*value_opt);
      return CommandResult::OK("CoreConfig::SetUploadFrequency()");
    }
    return CommandResult::Error("Invalid upload frequency!");
  }

  // set-config batch-processing-level <value>
  if (args.Peek() == "batch-processing-level") {
    std::string_view value_str = args[1];
    if (value_str.empty()) {
      return CommandResult::Error("No value supplied for batch-processing-level!");
    }
    if (auto value_opt = ParseBatchProcessingLevel(value_str)) {
      state.config.SetBatchProcessingLevel(*value_opt);
      return CommandResult::OK("CoreConfig::SetBatchProcessingLevel()");
    }
    return CommandResult::Error("Invalid batch processing level!");
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

  return CommandResult::Error("Unrecognized config option!");
}
