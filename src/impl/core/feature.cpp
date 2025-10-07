// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature.hpp"

#include "assert.hpp"

namespace datadog::impl {

void Feature::OnCoreStarted(FeatureScope&& feature_scope) {
  // Take ownership of our FeatureScope, which we can use as long as the Core is running
  DATADOG_ASSERT(!_scope, "Feature has non-null _scope in OnCoreStarted");
  _scope = std::move(feature_scope);

  // Notify the feature that the core is started
  Start();
}

void Feature::OnCoreStopping() {
  // Notify the feature that the core is about to stop, while it's still able to write
  // events
  Stop();

  // Drop our FeatureScope; the Core is stopped so we must stop all work
  DATADOG_ASSERT(_scope, "Feature has null _scope in OnCoreStop");
  _scope.reset();
}

bool Feature::WriteEvent(Block event, Block event_metadata) const {
  if (_scope) {
    return _scope->WriteEvent(event, event_metadata);
  }
  return false;
}

bool Feature::IsRunning() const { return _scope.has_value(); }

}  // namespace datadog::impl
