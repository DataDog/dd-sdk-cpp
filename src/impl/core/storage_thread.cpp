// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "assert.hpp"
#include "core/block.hpp"
#include "core/core.hpp"
#include "core/feature.hpp"
#include "core/storage_queue.hpp"
#include "core/storage_write.hpp"
#include "core/tlv.hpp"
#include "core/types.hpp"
#include "platform/filesystem.hpp"

namespace datadog::impl {

static void _handle_tracking_consent_changed(
    std::vector<RegisteredFeature>& features,
    const StorageMessage_TrackingConsentChanged& m
) {
  for (auto& feature : features) {
    if (!feature.event_storage->SetTrackingConsent(m.value)) {
      std::cout << "[STORAGE] ERROR: failed to handle tracking consent change "
                   "for feature "
                << feature.name << "\n";
    }
  }
}

static void _handle_event_generated(
    std::vector<RegisteredFeature>& features, const StorageMessage_EventGenerated& m
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

  // No RegisteredFeature should ever be initialized without a valid EventStorage
  DATADOG_ASSERT(
      feature->event_storage,
      "feature identified by generated event does not have a valid EventStorage"
  );

  // Use the RegisteredFeature's EventStorage to process the write operation, only
  // continuing once the filesystem write operations return
  const bool write_ok = feature->event_storage->HandleWrite(
      Block(reinterpret_cast<const char*>(m.event.data()), m.event.size()),  // NOLINT
      Block(
          reinterpret_cast<const char*>(m.event_metadata.data()),  // NOLINT
          m.event_metadata.size()
      )
  );

  // If the write operation failed, note the error, drop the event, and continue
  if (!write_ok) {
    std::cout << "[STORAGE] ERROR: write failed\n";
  }
}

void StorageThreadMain(StorageQueue& queue, std::vector<RegisteredFeature>& features) {
  std::cout << "[STORAGE] Started\n";

  // Perform continual blocking reads until we get std::nullopt, indicating that the
  // queue is drained and processing should stop
  while (const auto item = queue.Pop()) {
    switch (item->type) {
      case StorageMessageType::TrackingConsentChanged:
        _handle_tracking_consent_changed(
            features, item->payload.tracking_consent_changed
        );
        break;

      case StorageMessageType::EventGenerated:
        _handle_event_generated(features, item->payload.event_generated);
        break;
    }
  }

  std::cout << "[STORAGE] Finished\n";
}

}  // namespace datadog::impl
