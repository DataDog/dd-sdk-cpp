// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/core.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/diagnostics.hpp"
#include "datadog/impl/platform/system_info.hpp"

#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

/**
 * Shared test harness for capturing RUM events and diagnostics emitted by
 * implementation-layer scope tests.
 *
 * Captures ALL events (doesn't filter by type at capture time) and provides
 * filter methods to extract events by type after capture. Owns FeatureScope,
 * CoreContextProvider, and diagnostic infrastructure.
 */
class RumEventCapture {
  std::vector<nlohmann::json> all_events;
  DiagnosticMessageBuffer diagnostics;

  const char* application_id;
  const char* session_id;
  const char* view_id;

  platform::OsInfo os_info{"mock-os", "2.3.4", "mock-build-number", "2"};
  platform::DeviceInfo device_info{
      "desktop",
      "mock-device",
      "mock-model",
      "mock-brand",
      "x86_64",
      "en-US",
      "America/New_York"
  };

  CoreContextProvider context_provider;
  FeatureScope feature_scope;

 public:
  /**
   * Construct an event capture harness that validates application/session/view
   * IDs match expected values.
   *
   * Events with matching IDs are captured; ID validation is performed for types
   * that include those fields. If `view_id` is nullptr, view ID validation is
   * skipped (useful for session-level tests where view ID varies).
   */
  RumEventCapture(
      const char* application_id, const char* session_id, const char* view_id = nullptr
  )
      : application_id(application_id),
        session_id(session_id),
        view_id(view_id),
        context_provider(CoreContext(
            CoreConfig{"fake-client-token", "fake-service", "fake-env"},
            os_info,
            device_info
        )),
        feature_scope(
            context_provider,
            [this](Block event, Block event_metadata) {
              // RUM implementation doesn't produce events with metadata
              REQUIRE(event_metadata.empty());

              // Require valid JSON object
              auto obj = nlohmann::json::parse(event);
              REQUIRE(obj.is_object());

              // Validate IDs based on event type
              if (obj.contains("application") && obj["application"].contains("id")) {
                REQUIRE(obj["application"]["id"] == this->application_id);
              }
              if (obj.contains("session") && obj["session"].contains("id")) {
                REQUIRE(obj["session"]["id"] == this->session_id);
              }
              if (this->view_id != nullptr && obj.contains("view") &&
                  obj["view"].contains("id")) {
                REQUIRE(obj["view"]["id"] == this->view_id);
              }

              // Capture all events
              all_events.emplace_back(std::move(obj));
              return true;
            },
            DiagnosticLogger(
                [&](const DiagnosticMessage& message) {
                  switch (message.level) {
                    case DiagnosticLevel::Debug:
                      diagnostics.debug.emplace_back(message.text);
                      break;
                    case DiagnosticLevel::Status:
                      diagnostics.status.emplace_back(message.text);
                      break;
                    case DiagnosticLevel::Warning:
                      diagnostics.warning.emplace_back(message.text);
                      break;
                    case DiagnosticLevel::Error:
                      diagnostics.error.emplace_back(message.text);
                      break;
                  }
                },
                DiagnosticLevel::Debug
            )
        ) {}

  // Access methods
  FeatureScope& GetFeatureScope() { return feature_scope; }
  const DiagnosticMessageBuffer& Diagnostics() const { return diagnostics; }

  // Filter methods - return copies for easy assertions
  std::vector<nlohmann::json> FilterByType(std::string_view type) const {
    std::vector<nlohmann::json> result;
    for (const auto& event : all_events) {
      if (event.contains("type") && event["type"] == type) {
        result.push_back(event);
      }
    }
    return result;
  }

  std::vector<nlohmann::json> Vitals() const { return FilterByType("vital"); }

  std::vector<nlohmann::json> Views() const { return FilterByType("view"); }

  std::vector<nlohmann::json> Actions() const { return FilterByType("action"); }

  std::vector<nlohmann::json> Resources() const { return FilterByType("resource"); }

  std::vector<nlohmann::json> Errors() const { return FilterByType("error"); }

  std::vector<nlohmann::json> LongTasks() const { return FilterByType("long_task"); }
};
