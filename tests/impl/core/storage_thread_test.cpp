// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage_thread.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "datadog/impl/core/core.hpp"
#include "datadog/impl/storage/sdk.hpp"

#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"
#include "support/diagnostics.hpp"

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

  // Given a predictable, controllable system clock
  MockClock clock;

  // And a diagnostic logger that will capture all messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  // And a mock filesystem that will store SDK data at app/.datadog/
  MockFilesystem fs;
  fs.Mkdirs("app");

  // And a root storage directory for our SDK instance
  SdkStorage storage(fs, logger, 12345);
  REQUIRE(storage.Initialize("app", "main"));

  // And feature-specific storage directories for both our 'alpha' and 'bravo' features
  auto alpha_events = storage.InitializeFeatureEventStorage("alpha");
  REQUIRE(alpha_events != nullptr);
  auto bravo_events = storage.InitializeFeatureEventStorage("bravo");
  REQUIRE(bravo_events != nullptr);

  const std::string alpha_prefix = "app/.datadog/main/12345/alpha/";
  const std::string bravo_prefix = "app/.datadog/main/12345/bravo/";

  // And a function that will initialize a RegisteredFeature vector containing both
  // alpha and bravo, with the desired initial consent values
  auto init_features = [&](TrackingConsent alpha_consent,
                           TrackingConsent bravo_consent) {
    // Prepare a vector to hold the state for both features
    std::vector<RegisteredFeature> features;
    features.reserve(2);

    // Use default writer config
    auto writer_config = BatchWriterConfig::FromBatchSize(BatchSize::Small);

    // Create a BatchWriter for alpha and construct RegisteredFeature state
    auto alpha_writer = std::make_unique<BatchWriter>(
        logger, alpha_consent, fs, *alpha_events, clock, writer_config
    );
    features.emplace_back(
        CreateFeatureId("ALFA"),
        "alpha",
        std::move(alpha_events),
        std::make_shared<FeatureAlpha>(),
        std::move(alpha_writer),
        nullptr  // upload_state is exclusive to upload thread
    );

    // Create a BatchWriter for bravo and construct RegisteredFeature state
    auto bravo_writer = std::make_unique<BatchWriter>(
        logger, bravo_consent, fs, *bravo_events, clock, writer_config
    );
    features.emplace_back(
        CreateFeatureId("BRVO"),
        "bravo",
        std::move(bravo_events),
        std::make_shared<FeatureBravo>(),
        std::move(bravo_writer),
        nullptr  // upload_state is exclusive to upload thread
    );

    // Return the RegisteredFeature vector
    return features;
  };

  SECTION("M dispatch calls to appropriate features W queue contains messages") {
    // Given two registered features Alpha and Bravo, with initial tracking consent:
    // - Alpha (feature ID "ALFA"): NotGranted
    // - Bravo (feature ID "BRVO"): Granted
    auto features =
        init_features(TrackingConsent::NotGranted, TrackingConsent::Granted);
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
    StorageThreadMain(DiagnosticLogger{}, queue, features);

    // Then 'alpha/intermediate-v1' should contain 'alpha-1'
    auto alpha_pending_files = fs.Ls(alpha_prefix + "intermediate-v1");
    REQUIRE(alpha_pending_files.size() == 1);
    REQUIRE(
        fs.Cat(alpha_prefix + "intermediate-v1/" + alpha_pending_files.front()) ==
        MockTLVFile().AppendEvent("alpha-1").ToString()
    );

    // And 'alpha/v1' should be empty
    auto alpha_granted_files = fs.Ls(alpha_prefix + "v1");
    REQUIRE(alpha_granted_files.size() == 0);

    // And 'bravo/intermediate-v1' should contain 'bravo-1'
    auto bravo_pending_files = fs.Ls(bravo_prefix + "intermediate-v1");
    REQUIRE(bravo_pending_files.size() == 1);
    REQUIRE(
        fs.Cat(bravo_prefix + "intermediate-v1/" + bravo_pending_files.front()) ==
        MockTLVFile().AppendEvent("bravo-1").ToString()
    );

    // And 'bravo/v1' should contain 'bravo-0'
    auto bravo_granted_files = fs.Ls(bravo_prefix + "v1");
    REQUIRE(bravo_granted_files.size() == 1);
    REQUIRE(
        fs.Cat(bravo_prefix + "v1/" + bravo_granted_files.front()) ==
        MockTLVFile().AppendEvent("bravo-0").ToString()
    );
  }
}
