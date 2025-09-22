// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/storage_thread.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/core.hpp"
#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"

using namespace datadog;
using namespace datadog::impl;

class FeatureAlpha : public MockFeature {
 public:
  FeatureAlpha() : MockFeature(CreateFeatureId("ALFA"), "alpha") {}
};

class FeatureBravo : public MockFeature {
 public:
  FeatureBravo() : MockFeature(CreateFeatureId("BRVO"), "bravo") {}
};

TEST_CASE("StorageThreadMain", "[unit]") {
  // StorageThreadMain consumes from a thread-safe blocking queue and dispatches
  // function calls to the appropriate feature-specific state in response to message.

  // In these tests, we can push the desired set of messages into the queue, then stop
  // queue processing, and then our thread entrypoint can read from the queue until
  // it's drained.

  // Common test setup code: populate a RegisteredFeature vector suitable for use by
  // the storage thread
  auto init_features = [](TrackingConsent alpha_consent, TrackingConsent bravo_consent,
                          MockStorageDirectory& mock_storage, MockClock& clock,
                          std::vector<RegisteredFeature>& out_features) -> void {
    // Mock storage for Feature Alpha
    auto alpha = mock_storage.PrepareSubdirectory("alpha");
    REQUIRE(alpha.has_value());
    auto alpha_pending = (*alpha)->PrepareSubdirectory("no-upload");
    REQUIRE(alpha_pending.has_value());
    auto alpha_granted = (*alpha)->PrepareSubdirectory("yes-upload");
    REQUIRE(alpha_granted.has_value());

    // Mock storage for Feature Bravo
    auto bravo = mock_storage.PrepareSubdirectory("bravo");
    REQUIRE(bravo.has_value());
    auto bravo_pending = (*bravo)->PrepareSubdirectory("no-upload");
    REQUIRE(bravo_pending.has_value());
    auto bravo_granted = (*bravo)->PrepareSubdirectory("yes-upload");
    REQUIRE(bravo_granted.has_value());

    // Use default writer config
    auto writer_config = BatchWriterConfig::FromBatchSize(BatchSize::Small);

    // "Register" Alpha
    out_features.emplace_back(
        CreateFeatureId("ALFA"), "alpha", std::make_shared<FeatureAlpha>(),
        std::move(*alpha),
        std::make_unique<EventStorage>(
            alpha_consent,
            std::make_unique<BatchWriter>(
                std::move(*alpha_pending), clock, writer_config
            ),
            std::make_unique<BatchWriter>(
                std::move(*alpha_granted), clock, writer_config
            )
        ),
        nullptr,  // event_read_directory is exclusive to upload thread
        nullptr   // upload_state is exclusive to upload thread
    );

    // "Register" Bravo
    out_features.emplace_back(
        CreateFeatureId("BRVO"), "bravo", std::make_shared<FeatureBravo>(),
        std::move(*bravo),
        std::make_unique<EventStorage>(
            bravo_consent,
            std::make_unique<BatchWriter>(
                std::move(*bravo_pending), clock, writer_config
            ),
            std::make_unique<BatchWriter>(
                std::move(*bravo_granted), clock, writer_config
            )
        ),
        nullptr,  // event_read_directory is exclusive to upload thread
        nullptr   // upload_state is exclusive to upload thread
    );
  };

  SECTION("M dispatch calls to appropriate features W queue contains messages") {
    // Given two registered features Alpha and Bravo, with initial tracking consent:
    // - Alpha (feature ID "ALFA"): NotGranted
    // - Bravo (feature ID "BRVO"): Granted
    MockStorageDirectory mock_storage;
    MockClock clock;
    std::vector<RegisteredFeature> features;
    init_features(
        TrackingConsent::NotGranted, TrackingConsent::Granted, mock_storage, clock,
        features
    );
    REQUIRE(features.size() == 2);

    // And a queue to which the following messages have been produced:
    // - Handle write from "ALFA" (should be ignored)
    // - Handle write from "BRVO" (should be written to granted dir)
    // - Change tracking consent to Pending for all features
    // - Handle write from "ALFA" (should be written to pending dir)
    // - Handle write from "BRVO" (should be written to pending dir)
    StorageQueue queue;
    REQUIRE(queue.Push(
        StorageMessage::EventGenerated(CreateFeatureId("ALFA"), "alpha-0", {})
    ));
    REQUIRE(queue.Push(
        StorageMessage::EventGenerated(CreateFeatureId("BRVO"), "bravo-0", {})
    ));
    REQUIRE(
        queue.Push(StorageMessage::TrackingConsentChanged(TrackingConsent::Pending))
    );
    REQUIRE(queue.Push(
        StorageMessage::EventGenerated(CreateFeatureId("ALFA"), "alpha-1", {})
    ));
    REQUIRE(queue.Push(
        StorageMessage::EventGenerated(CreateFeatureId("BRVO"), "bravo-1", {})
    ));

    // When we run the upload thread and drain the queue
    queue.Stop();
    StorageThreadMain(queue, features);

    // Then 'alpha/no-upload' should contain 'alpha-1'
    auto alpha_pending_files = mock_storage.FindFiles("alpha/no-upload");
    REQUIRE(alpha_pending_files.size() == 1);
    REQUIRE(
        mock_storage.Cat(alpha_pending_files.front()) ==
        MockTLVFile().AppendEvent("alpha-1").ToString()
    );

    // And 'alpha/yes-upload' should be empty
    auto alpha_granted_files = mock_storage.FindFiles("alpha/yes-upload");
    REQUIRE(alpha_granted_files.size() == 0);

    // And 'bravo/no-upload' should contain 'bravo-1'
    auto bravo_pending_files = mock_storage.FindFiles("bravo/no-upload");
    REQUIRE(bravo_pending_files.size() == 1);
    REQUIRE(
        mock_storage.Cat(bravo_pending_files.front()) ==
        MockTLVFile().AppendEvent("bravo-1").ToString()
    );

    // And 'bravo/yes-upload' should contain 'bravo-0'
    auto bravo_granted_files = mock_storage.FindFiles("bravo/yes-upload");
    REQUIRE(bravo_granted_files.size() == 1);
    REQUIRE(
        mock_storage.Cat(bravo_granted_files.front()) ==
        MockTLVFile().AppendEvent("bravo-0").ToString()
    );
  }
}
