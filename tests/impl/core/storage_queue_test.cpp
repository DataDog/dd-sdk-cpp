// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage_queue.hpp"

#include <catch2/catch_test_macros.hpp>
#include <future>
#include <memory>
#include <vector>

#include "datadog/impl/core/core.hpp"

#include "mock/feature.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("StorageMessage", "[unit]") {
  SECTION("M create TrackingConsentChanged message W static factory method used") {
    // Given a tracking consent value
    TrackingConsent consent = TrackingConsent::Granted;

    // When creating a TrackingConsentChanged message
    auto message = StorageMessage::TrackingConsentChanged(consent);

    // Then the message has the correct type and payload
    REQUIRE(message.type == StorageMessageType::TrackingConsentChanged);
    REQUIRE(message.payload.tracking_consent_changed.value == TrackingConsent::Granted);
  }

  SECTION("M create EventGenerated message W static factory method used") {
    // Given feature data and event blocks
    FeatureId feature_id = CreateFeatureId("TEST");
    Block event_data{"test event data"};
    Block metadata_data{"test metadata"};

    // When creating an EventGenerated message
    auto message =
        StorageMessage::EventGenerated(feature_id, event_data, metadata_data, false);

    // Then the message has the correct type and payload
    REQUIRE(message.type == StorageMessageType::EventGenerated);
    REQUIRE(message.payload.event_generated.feature_id == feature_id);

    // And the event data is copied correctly
    std::vector<uint8_t> expected_event(event_data.begin(), event_data.end());
    REQUIRE(message.payload.event_generated.event == expected_event);

    // And the metadata is copied correctly
    std::vector<uint8_t> expected_metadata(metadata_data.begin(), metadata_data.end());
    REQUIRE(message.payload.event_generated.event_metadata == expected_metadata);
  }

  SECTION("M handle empty metadata W EventGenerated message created") {
    // Given feature data with empty metadata
    FeatureId feature_id = CreateFeatureId("LOGS");
    Block event_data{"event without metadata"};
    Block empty_metadata{};

    // When creating an EventGenerated message with empty metadata
    auto message =
        StorageMessage::EventGenerated(feature_id, event_data, empty_metadata, false);

    // Then the message is created successfully with empty metadata
    REQUIRE(message.type == StorageMessageType::EventGenerated);
    REQUIRE(message.payload.event_generated.event_metadata.empty());
    REQUIRE(!message.payload.event_generated.event.empty());
  }

  SECTION("M move construct properly W union contains TrackingConsentChanged") {
    // Given a TrackingConsentChanged message
    auto original = StorageMessage::TrackingConsentChanged(TrackingConsent::Pending);

    // When move constructing from it
    auto moved = std::move(original);

    // Then the moved message has the correct data
    REQUIRE(moved.type == StorageMessageType::TrackingConsentChanged);
    REQUIRE(moved.payload.tracking_consent_changed.value == TrackingConsent::Pending);
  }

  SECTION("M move construct properly W union contains EventGenerated") {
    // Given an EventGenerated message with significant data
    FeatureId feature_id = CreateFeatureId("MOVE");
    Block event_data{"data to be moved"};
    Block metadata_data{"metadata to move"};
    auto original =
        StorageMessage::EventGenerated(feature_id, event_data, metadata_data, false);

    // When move constructing from it
    auto moved = std::move(original);

    // Then the moved message has the correct data
    REQUIRE(moved.type == StorageMessageType::EventGenerated);
    REQUIRE(moved.payload.event_generated.feature_id == feature_id);

    std::vector<uint8_t> expected_event(event_data.begin(), event_data.end());
    REQUIRE(moved.payload.event_generated.event == expected_event);

    std::vector<uint8_t> expected_metadata(metadata_data.begin(), metadata_data.end());
    REQUIRE(moved.payload.event_generated.event_metadata == expected_metadata);
  }

  SECTION("M move assign properly W source and destination have same union type") {
    // Given two TrackingConsentChanged messages
    auto source = StorageMessage::TrackingConsentChanged(TrackingConsent::Granted);
    auto destination =
        StorageMessage::TrackingConsentChanged(TrackingConsent::NotGranted);

    // When move assigning
    destination = std::move(source);

    // Then the destination has the source's data
    REQUIRE(destination.type == StorageMessageType::TrackingConsentChanged);
    REQUIRE(
        destination.payload.tracking_consent_changed.value == TrackingConsent::Granted
    );
  }

  SECTION(
      "M move assign properly W source and destination have different union types"
  ) {
    // Given a TrackingConsentChanged message and an EventGenerated message
    auto consent_msg = StorageMessage::TrackingConsentChanged(TrackingConsent::Pending);
    FeatureId feature_id = CreateFeatureId("SWAP");
    Block event_data{"swapped event"};
    Block metadata_data{"swapped meta"};
    auto event_msg =
        StorageMessage::EventGenerated(feature_id, event_data, metadata_data, false);

    // When move assigning from different union types
    consent_msg = std::move(event_msg);

    // Then the destination correctly adopts the source's type and data
    REQUIRE(consent_msg.type == StorageMessageType::EventGenerated);
    REQUIRE(consent_msg.payload.event_generated.feature_id == feature_id);

    std::vector<uint8_t> expected_event(event_data.begin(), event_data.end());
    REQUIRE(consent_msg.payload.event_generated.event == expected_event);

    std::vector<uint8_t> expected_metadata(metadata_data.begin(), metadata_data.end());
    REQUIRE(consent_msg.payload.event_generated.event_metadata == expected_metadata);
  }

  SECTION("M properly destruct W union contains EventGenerated with vectors") {
    // Given an EventGenerated message in a limited scope
    {
      FeatureId feature_id = CreateFeatureId("DEST");
      Block large_event_data{
          "this is a large event data block that will test destruction"
      };
      Block large_metadata{"large metadata block for destruction testing"};

      // When creating and then destroying the message
      auto message = StorageMessage::EventGenerated(
          feature_id, large_event_data, large_metadata, false
      );

      // Then destruction should happen without issues when message goes out of
      // scope
      REQUIRE(message.type == StorageMessageType::EventGenerated);
    }
    // Destructor is called here - test passes if no crashes occur
  }

  SECTION("M create FlushWork message W static factory method used") {
    // Given a shared promise that the storage thread will fulfill
    auto promise = std::make_shared<std::promise<void>>();
    const auto* raw = promise.get();

    // When creating a FlushWork message via the factory
    auto message = StorageMessage::FlushWork(promise);

    // Then the message has the correct type and the payload owns the same promise
    REQUIRE(message.type == StorageMessageType::FlushWork);
    REQUIRE(message.payload.flush.done.get() == raw);
  }

  SECTION("M move construct properly W union contains FlushWork") {
    // Given a FlushWork message carrying a shared promise, and a future obtained from
    // that promise before the move
    auto promise = std::make_shared<std::promise<void>>();
    std::future<void> future = promise->get_future();
    auto original = StorageMessage::FlushWork(std::move(promise));

    // When move constructing from it
    auto moved = std::move(original);

    // Then the moved message retains the type and owns the promise
    REQUIRE(moved.type == StorageMessageType::FlushWork);
    REQUIRE(moved.payload.flush.done != nullptr);

    // And fulfilling via the moved-into message unblocks the caller's future
    moved.payload.flush.done->set_value();
    REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
  }

  SECTION(
      "M move assign properly W source is FlushWork and destination is EventGenerated"
  ) {
    // Given a destination EventGenerated message and a source FlushWork message;
    // capture a future from the FlushWork's promise before the move
    FeatureId feature_id = CreateFeatureId("SWAP");
    Block event_data{"to be overwritten"};
    auto destination =
        StorageMessage::EventGenerated(feature_id, event_data, {}, false);
    auto promise = std::make_shared<std::promise<void>>();
    std::future<void> future = promise->get_future();
    auto source = StorageMessage::FlushWork(std::move(promise));

    // When move-assigning across types
    destination = std::move(source);

    // Then the destination correctly adopts the FlushWork type and promise
    REQUIRE(destination.type == StorageMessageType::FlushWork);
    REQUIRE(destination.payload.flush.done != nullptr);

    // And the carried promise still drives the original future
    destination.payload.flush.done->set_value();
    REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
  }

  SECTION("M properly destruct W union contains FlushWork") {
    // Given a FlushWork message in a limited scope; capture a future before destruction
    auto promise = std::make_shared<std::promise<void>>();
    std::future<void> future = promise->get_future();
    {
      auto message = StorageMessage::FlushWork(promise);
      REQUIRE(message.type == StorageMessageType::FlushWork);
    }
    // Destruction releases the message's reference to the promise; this thread's
    // shared_ptr (`promise`) still owns it, so the future state is intact
    REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
    promise->set_value();
    REQUIRE(future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
  }
}
