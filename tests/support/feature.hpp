// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <string>
#include <vector>

#include "assert.hpp"
#include "core/context.hpp"
#include "core/feature.hpp"
#include "core/feature_scope.hpp"

using namespace datadog::impl;

struct CapturedEvent {
  std::string data;
  std::string metadata;

  explicit CapturedEvent(Block event, Block event_metadata)
      : data(event), metadata(event_metadata) {}
};

/**
 * Wraps a FeatureScope, providing Feature implementations under test with a
 * test-controlled CoreContext value, and capturing all events produced by the feature.
 */
class FeatureTest {
  CoreContextProvider _context_provider;

 public:
  std::vector<CapturedEvent> events;

  explicit FeatureTest(const CoreContext& context) : _context_provider(context) {}

  void Start(const std::shared_ptr<Feature>& feature) {
    auto event_generated_func = [this](Block event, Block event_metadata) {
      events.emplace_back(event, event_metadata);
      return true;
    };
    feature->OnCoreStarted(FeatureScope(_context_provider, event_generated_func));
  }

  void Stop(const std::shared_ptr<Feature>& feature) { feature->OnCoreStopping(); }
};
