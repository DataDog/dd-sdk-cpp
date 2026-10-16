// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <vector>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/types/assert.hpp"

using namespace datadog;
using namespace datadog::impl;

struct CapturedEvent {
  std::string data;
  std::string metadata;

  explicit CapturedEvent(Block event, Block event_metadata)
      : data(event), metadata(event_metadata) {}
};

struct CapturedDiagnosticMessage {
  DiagnosticLevel level;
  std::string text;

  explicit CapturedDiagnosticMessage(const DiagnosticMessage& message)
      : level(message.level), text(message.text) {}
};

/**
 * Wraps a FeatureScope, providing Feature implementations under test with a
 * test-controlled CoreContext value, and capturing all events produced by the feature.
 */
class FeatureTest {
  CoreContextProvider _context_provider;

 public:
  std::vector<CapturedEvent> events;
  std::vector<FeatureMessage> feature_messages;
  std::vector<CapturedDiagnosticMessage> diagnostic_messages;

  explicit FeatureTest(const CoreContext& context) : _context_provider(context) {}

  void Start(const std::shared_ptr<Feature>& feature) {
    // Mirror Core::Start() by resetting feature context before each run, so tests
    // observe the same clean-slate guarantee that production code provides
    _context_provider.Update([](CoreContext& ctx) { ctx.Reset(); });

    auto event_writer =
        [this](Block event, Block event_metadata, bool /*bypass_tracking_consent*/) {
          events.emplace_back(event, event_metadata);
          return true;
        };
    auto message_publisher = [this](FeatureMessage message) {
      feature_messages.emplace_back(std::move(message));
      return true;
    };
    auto diagnostic_handler = [this](const DiagnosticMessage& message) {
      diagnostic_messages.emplace_back(message);
    };
    feature->OnCoreStarted(
        FeatureScope::CreateForTesting(
            _context_provider,
            event_writer,
            message_publisher,
            DiagnosticLogger(diagnostic_handler, DiagnosticLevel::Debug)
        )
    );
  }

  void Stop(const std::shared_ptr<Feature>& feature) { feature->OnCoreStopping(); }

  void UpdateContext(const std::function<void(CoreContext&)>& callback) {
    _context_provider.Update(callback);
  }

  inline CoreContext GetContextSync() const { return _context_provider.Get(); }
};
