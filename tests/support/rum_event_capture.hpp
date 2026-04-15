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
#include "datadog/impl/core/platform/system_info.hpp"
#include "datadog/impl/core/util/diagnostics.hpp"

#include "support/context.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

/**
 * Shared test harness for capturing RUM events and diagnostics emitted by
 * implementation-layer scope tests.
 *
 * Captures ALL events (doesn't filter by type at capture time) and provides filter
 * methods to extract events by type after capture. Owns FeatureScope,
 * CoreContextProvider, and diagnostic logging code.
 */
class RumEventCapture {
  std::vector<nlohmann::json> all_events;
  DiagnosticMessageBuffer diagnostics;

  const char* application_id;
  const char* session_id;
  const char* view_id;

  CoreContextProvider context_provider;
  EventWriter _event_func;
  FeatureScope feature_scope;

 public:
  /**
   * Constructs a new event capture harness that validates application/session/view
   * IDs match expected values, and buffers all RUM events and diagnostic messages
   * emitted during a test.
   *
   * As each event is captured, this class will validate that it's a valid JSON object,
   * and that if has an "application.id" and "session.id" value matching the values
   * given on construction. If a view_id value is given, and the event has a "view.id"
   * value, it will be checked for equality as well.
   */
  RumEventCapture(
      const char* application_id, const char* session_id, const char* view_id = nullptr
  )
      : application_id(application_id),
        session_id(session_id),
        view_id(view_id),
        context_provider(CoreContext(
            CoreConfig{"fake-client-token", "fake-service", "fake-env"},
            MOCK_OS_INFO,
            MOCK_DEVICE_INFO
        )),
        _event_func([this](Block event, Block event_metadata) {
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
        }),
        feature_scope(
            FeatureScope::CreateForTesting(
                context_provider,
                _event_func,
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
            )
        ) {}

  /**
   * Returns a reference to the FeatureScope owned by this harness, so that it can be
   * injected into the code under test.
   */
  FeatureScope& GetFeatureScope() { return feature_scope; }

  /**
   * Returns the current CoreContext snapshot for use as a test argument.
   */
  CoreContext GetContext() const { return context_provider.Get(); }

  /**
   * Returns the EventWriter for use as a test argument.
   */
  const EventWriter& GetWriter() const { return _event_func; }

  /**
   * Returns the full set of diagnostic messages that were emitted via this object's
   * FeatureScope since construction.
   */
  const DiagnosticMessageBuffer& Diagnostics() const { return diagnostics; }

  /**
   * Returns the set of captured events whose top-level "type" property matches the
   * given values.
   */
  std::vector<nlohmann::json> FilterByType(std::string_view type) const {
    std::vector<nlohmann::json> result;
    for (const auto& event : all_events) {
      if (event.contains("type") &&
          event["type"].get_ref<const std::string&>() == type) {
        result.push_back(event);
      }
    }
    return result;
  }

  /**
   * Returns all RUM View events that have been captured.
   */
  std::vector<nlohmann::json> Views() const { return FilterByType("view"); }

  /**
   * Returns all RUM Action events that have been captured.
   */
  std::vector<nlohmann::json> Actions() const { return FilterByType("action"); }

  /**
   * Returns all RUM Resource events that have been captured.
   */
  std::vector<nlohmann::json> Resources() const { return FilterByType("resource"); }

  /**
   * Returns all RUM Error events that have been captured.
   */
  std::vector<nlohmann::json> Errors() const { return FilterByType("error"); }

  /**
   * Returns all RUM Vital events that have been captured.
   */
  std::vector<nlohmann::json> Vitals() const { return FilterByType("vital"); }
};
