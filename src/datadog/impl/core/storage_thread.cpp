// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <vector>

#include "datadog/impl/assert.hpp"
#include "datadog/impl/core/block.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/core/storage_queue.hpp"

namespace datadog::impl {

static void _handle_tracking_consent_changed(
    const DiagnosticLogger& diagnostic_logger,
    std::vector<RegisteredFeature>& features,
    const StorageMessage_TrackingConsentChanged& m
) {
  for (auto& feature : features) {
    if (!feature.event_storage->SetConsent(m.value)) {
      diagnostic_logger.Error(
          "Failed to handle tracking consent change", {{"feature", feature.name}}
      );
    }
  }
}

static void _handle_event_generated(
    const DiagnosticLogger& diagnostic_logger,
    std::vector<RegisteredFeature>& features,
    const StorageMessage_EventGenerated& m
) {
  // Find the feature implementation identified in the message
  const auto feature =
      std::find_if(features.begin(), features.end(), [&](const RegisteredFeature& f) {
        return f.id == m.feature_id;
      });

  // Ignore the message if no such feature exists
  if (feature == features.end()) {
    DATADOG_ASSERT(
        false, "feature_id on generated event matches no registered feature"
    );
    return;
  }

  DATADOG_ASSERT(
      feature->event_storage,
      "feature identified by generated event does not have a valid EventStorage"
  );

  const bool write_ok = feature->event_storage->Write(
      Block(reinterpret_cast<const char*>(m.event.data()), m.event.size()),  // NOLINT
      Block(
          reinterpret_cast<const char*>(m.event_metadata.data()),  // NOLINT
          m.event_metadata.size()
      )
  );

  // If the write operation failed, note the error, drop the event, and continue
  if (!write_ok) {
    diagnostic_logger.Error(
        "Failed to write event to storage",
        {{"feature", feature->name},
         {"event_size", m.event.size()},
         {"event_metadata_size", m.event_metadata.size()}}
    );
  } else {
    diagnostic_logger.Debug(
        "Event written to storage",
        {{"feature", feature->name},
         {"event_size", m.event.size()},
         {"event_metadata_size", m.event_metadata.size()}}
    );
  }
}

void StorageThreadMain(
    const DiagnosticLogger& diagnostic_logger,
    StorageQueue& queue,
    std::vector<RegisteredFeature>& features
) {
  diagnostic_logger.Debug("Storage thread starting");

  // Perform continual blocking reads until we get std::nullopt, indicating that the
  // queue is drained and processing should stop
  while (const auto item = queue.Pop()) {
    switch (item->type) {
      case StorageMessageType::TrackingConsentChanged:
        _handle_tracking_consent_changed(
            diagnostic_logger, features, item->payload.tracking_consent_changed
        );
        break;

      case StorageMessageType::EventGenerated:
        _handle_event_generated(
            diagnostic_logger, features, item->payload.event_generated
        );
        break;
    }
  }

  // Close any open batch file handles so the upload thread can delete the files.
  // On Windows, open file handles prevent deletion, so we must close before signaling
  // that the storage thread is done.
  for (auto& feature : features) {
    if (feature.event_storage) {
      feature.event_storage->Flush();
    }
  }

  diagnostic_logger.Debug("Storage thread finished");
}

}  // namespace datadog::impl
