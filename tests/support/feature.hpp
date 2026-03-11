// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <vector>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/feature_scope.hpp"

using namespace datadog;
using namespace datadog::impl;

struct CapturedEvent {
  std::string data;
  std::string metadata;

  explicit CapturedEvent(Block event, Block event_metadata)
      : data(event), metadata(event_metadata) {}
};

struct CapturedMessage {
  DiagnosticLevel level;
  std::string text;

  explicit CapturedMessage(const DiagnosticMessage& message)
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
  std::vector<CapturedMessage> messages;

  explicit FeatureTest(const CoreContext& context) : _context_provider(context) {}

  void Start(const std::shared_ptr<Feature>& feature) {
    auto event_writer = [this](Block event, Block event_metadata) {
      events.emplace_back(event, event_metadata);
      return true;
    };
    auto diagnostic_handler = [this](const DiagnosticMessage& message) {
      messages.emplace_back(message);
    };
    feature->OnCoreStarted(
        FeatureScope::CreateForTesting(
            _context_provider,
            event_writer,
            DiagnosticLogger(diagnostic_handler, DiagnosticLevel::Debug)
        )
    );
  }

  void Stop(const std::shared_ptr<Feature>& feature) { feature->OnCoreStopping(); }

  inline CoreContext GetContextSync() const { return _context_provider.Get(); }
};
