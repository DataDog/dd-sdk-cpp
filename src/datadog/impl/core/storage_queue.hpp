// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cinttypes>
#include <utility>
#include <vector>

#include "datadog/core.hpp"

#include "datadog/impl/core/feature.hpp"
#include "datadog/impl/core/queue.hpp"

namespace datadog::impl {

/**
 * Type identifier for an internal message that can be produced to the storage queue in
 * order to push data to the storage thread.
 *
 * @note If adding a new message type, take care to:
 *
 * 1. Define a payload type `struct StorageMessage_<NewTypeName>`
 * 2. Add that type as a member of the `StorageMessage::Payload` union
 * 3. Add a branch to `~StorageMessage()` to explicitly handle the destruction of
 *    `payload.<new_type_name>` when `type == StorageMessageType:<NewTypeName>`: if
 *    payload type is trivially destructible, static_assert so; if it owns memory or
 *    manages resources, invoke its destructor on `payload.<new_type_name>` explicitly.
 * 4. Add a branch to `StorageMessage(StorageMessage&&)` to handle move construction
 *    with payloads of the new type
 * 5. Add a branch to `operator= (StorageMessage&&)` to handle move-assignment with
 *    payloads of the new type
 * 6. Add a static function `StorageMessage <NewTypeName>(...)` to initialize messages
 *    of the new type.
 *
 * We might avoid this complexity by switching to std::variant.
 */
enum class StorageMessageType : uint8_t {
  TrackingConsentChanged,
  EventGenerated,
};

/**
 * An API call has changed the configured user tracking consent value.
 */
struct StorageMessage_TrackingConsentChanged {
  /**
   * Whether the user has consented to tracking.
   */
  TrackingConsent value;
};

/**
 * A feature implementation has produced an event that should be flushed to persistent
 * storage.
 */
struct StorageMessage_EventGenerated {
  /**
   * Unique FourCC identifier of the feature that produced this message. The
   * accompanying event data will be written in the storage directory that corresponds
   * to this feature.
   */
  FeatureId feature_id;
  /**
   * The binary data representing the payload of this event, to be written to storage as
   * a TLV block of type 'Event'. The message stores a copy of the source data.
   */
  std::vector<uint8_t> event;
  /**
   * An optional payload describing the event, also copied. Will be written as a TLV
   * block of type 'Metadata', immediately preceiding the Event block, if and only if
   * non-empty.
   */
  std::vector<uint8_t> event_metadata;
  /**
   * When `true`, the event will be written directly to the granted-consent storage
   * directory regardless of the current `TrackingConsent` value.
   */
  bool bypass_tracking_consent;

  /**
   * Constructs a new 'EventGenerated' message, copying the provided event data.
   */
  explicit StorageMessage_EventGenerated(
      FeatureId in_feature_id,
      Block in_event,
      Block in_event_metadata,
      bool in_bypass_tracking_consent
  );
};

/**
 * A message sent to the storage thread.
 */
struct StorageMessage {
  /**
   * Actual data associated with a message; discriminated by StorageMessage::type.
   *
   * If adding a new payload type that is not trivially destructible, you MUST ensure
   * that `~StorageMessage()` manually invokes the destructor on the appropriate union
   * member when the message has the corresponding type.
   *
   * For clarity: if your payload type owns memory (or manages resources) such that its
   * destructor must be called when it goes out of scope (regardless of whether that
   * destructor is explicitly-defined or compiler-generated), then it is NOT trivially
   * destructible and you MUST invoke its destructor explicitly in `~StorageMessage()`.
   */
  union Payload {
    StorageMessage_TrackingConsentChanged tracking_consent_changed;
    StorageMessage_EventGenerated event_generated;

    /**
     * Messages are initialized using static functions that use placement new to
     * initialize payloads; let the compiler know that it's OK for a Payload to exist in
     * a non-initialized state.
     */
    Payload() {};  // NOLINT(cppcoreguidelines-pro-type-member-init)

    /**
     * Deletion of union members is explicitly handled by ~StorageMessage(); define an
     * empty union destructor to signal this fact to the compiler.
     */
    ~Payload() {};

    // Payload itself should have no compiler-generated copy/move operations
    Payload(const Payload&) = delete;
    Payload(Payload&&) = delete;
    Payload& operator=(const Payload&) = delete;
    Payload& operator=(Payload&&) = delete;
  };

  StorageMessageType type;
  Payload payload;

 private:
  explicit StorageMessage(StorageMessageType in_type) : type(in_type) {}

 public:
  ~StorageMessage();

  // Copying is disallowed; storage queue moves messages in and out
  StorageMessage(const StorageMessage&) = delete;
  StorageMessage& operator=(const StorageMessage&) = delete;

  // Moving is defined as a move on the appropriate union member
  StorageMessage(StorageMessage&& other) noexcept;
  StorageMessage& operator=(StorageMessage&& other) noexcept;

  /**
   * Creates a new TrackingConsentChanged message.
   */
  static StorageMessage TrackingConsentChanged(TrackingConsent value);

  /**
   * Creates a new EventGenerated message.
   */
  static StorageMessage EventGenerated(
      FeatureId feature_id,
      Block event,
      Block event_metadata,
      bool bypass_tracking_consent
  );
};

/**
 * Thread-safe queue used to push data into the storage thread.
 */
using StorageQueue = Queue<StorageMessage>;

}  // namespace datadog::impl
