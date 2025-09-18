// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/feature.hpp"

#include "assert.hpp"

namespace datadog::impl {

void Feature::OnCoreStarted(const EventGeneratedFunc& event_callback) {
  // We're now permitted to write events; store a reference to our writer callback
  DATADOG_ASSERT(
      !_event_callback, "Feature has non-null _event_callback in OnCoreStarted"
  );
  DATADOG_ASSERT(
      event_callback, "Feature received null event_callback in OnCoreStarted"
  );
  _event_callback = event_callback;

  // Notify the feature that the core is started
  Start();
}

void Feature::OnCoreStopping() {
  // Notify the feature that the core is about to stop, while it's still able to write
  // events
  Stop();

  // Clear the writer callback; we're no longer permitted to write anything
  DATADOG_ASSERT(_event_callback, "Feature has null _event_callback in OnCoreStop");
  _event_callback = nullptr;
}

bool Feature::WriteEvent(Block event, Block event_metadata) const {
  if (_event_callback) {
    return _event_callback(event, event_metadata);
  }
  return false;
}

bool Feature::IsRunning() const { return _event_callback != nullptr; }

}  // namespace datadog::impl
