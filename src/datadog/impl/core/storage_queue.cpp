// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage_queue.hpp"

#include "datadog/impl/core/block.hpp"

namespace datadog::impl {

StorageMessage_EventGenerated::StorageMessage_EventGenerated(
    FeatureId in_feature_id, Block in_event, Block in_event_metadata
)
    : feature_id(in_feature_id),
      event(in_event.begin(), in_event.end()),
      event_metadata(in_event_metadata.begin(), in_event_metadata.end()) {}

StorageMessage::~StorageMessage() {
  // When a union value goes out of the scope, the compiler can't know which
  // destructor to invoke: ensure that a Payload is always cleaned up
  switch (type) {
    case StorageMessageType::TrackingConsentChanged:
      static_assert(
          std::is_trivially_destructible<StorageMessage_TrackingConsentChanged>::value
      );
      break;

    case StorageMessageType::EventGenerated:
      payload.event_generated.~StorageMessage_EventGenerated();
      break;
  }
}

StorageMessage::StorageMessage(StorageMessage&& other) noexcept : type(other.type) {
  // When a union value is moved, the compiler can't know which member's move
  // constructor to invoke: ensure that a Payload is moved correctly based on type,
  // using placement new to construct the new value in-place
  switch (type) {
    case StorageMessageType::TrackingConsentChanged:
      static_assert(
          std::is_trivially_copyable<StorageMessage_TrackingConsentChanged>::value
      );
      payload.tracking_consent_changed = other.payload.tracking_consent_changed;
      break;

    case StorageMessageType::EventGenerated:
      new (&payload.event_generated)
          StorageMessage_EventGenerated(std::move(other.payload.event_generated));
      break;
  }
}

StorageMessage& StorageMessage::operator=(StorageMessage&& other) noexcept {
  // Handle move-assignment as in the move constructor; cleaning up the destination
  // value first
  if (this != &other) {
    // Destroy the existing payload
    this->~StorageMessage();

    // Move-construct the new payload in-place
    type = other.type;
    switch (type) {
      case StorageMessageType::TrackingConsentChanged:
        static_assert(
            std::is_trivially_copyable<StorageMessage_TrackingConsentChanged>::value
        );
        payload.tracking_consent_changed = other.payload.tracking_consent_changed;
        break;

      case StorageMessageType::EventGenerated:
        new (&payload.event_generated)
            StorageMessage_EventGenerated(std::move(other.payload.event_generated));
        break;
    }
  }
  return *this;
}

StorageMessage StorageMessage::TrackingConsentChanged(TrackingConsent value) {
  StorageMessage m{StorageMessageType::TrackingConsentChanged};
  new (&m.payload.tracking_consent_changed)
      StorageMessage_TrackingConsentChanged{value};
  return m;
}

StorageMessage StorageMessage::EventGenerated(
    FeatureId feature_id, Block event, Block event_metadata
) {
  StorageMessage m{StorageMessageType::EventGenerated};
  new (&m.payload.event_generated)
      StorageMessage_EventGenerated{feature_id, event, event_metadata};
  return m;
}

}  // namespace datadog::impl
