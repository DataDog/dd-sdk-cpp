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
#include "datadog/impl/storage/event.hpp"

#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem_new.hpp"
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

static const std::string EVENTS_ROOT = "/events";

TEST_CASE("StorageThreadMain", "[unit]") {
  // StorageThreadMain consumes from a thread-safe blocking queue and dispatches
  // function calls to the appropriate feature-specific state in response to messages.

  // In these tests, we push the desired set of messages into the queue, stop queue
  // processing, and then run the thread entrypoint to drain the queue.

  // Common test setup: populate a RegisteredFeature vector for the storage thread
  auto init_features = [](TrackingConsent alpha_consent,
                          TrackingConsent bravo_consent,
                          impl::MockFilesystem& fs,
                          MockClock& clock,
                          std::vector<RegisteredFeature>& out_features) -> void {
    fs.Mkdirs(EVENTS_ROOT);

    auto alpha_storage = std::make_unique<EventStorage>(
        fs,
        "alpha",
        DiagnosticLogger{},
        alpha_consent,
        clock,
        EventStorageConfig::FromBatchSize(BatchSize::Small)
    );
    REQUIRE(alpha_storage->Initialize(EVENTS_ROOT));

    auto bravo_storage = std::make_unique<EventStorage>(
        fs,
        "bravo",
        DiagnosticLogger{},
        bravo_consent,
        clock,
        EventStorageConfig::FromBatchSize(BatchSize::Small)
    );
    REQUIRE(bravo_storage->Initialize(EVENTS_ROOT));

    out_features.emplace_back(
        CreateFeatureId("ALFA"),
        "alpha",
        std::make_shared<FeatureAlpha>(),
        std::move(alpha_storage),
        nullptr,  // event_read_directory is exclusive to upload thread
        nullptr   // upload_state is exclusive to upload thread
    );
    out_features.emplace_back(
        CreateFeatureId("BRVO"),
        "bravo",
        std::make_shared<FeatureBravo>(),
        std::move(bravo_storage),
        nullptr,  // event_read_directory is exclusive to upload thread
        nullptr   // upload_state is exclusive to upload thread
    );
  };

  SECTION("M dispatch calls to appropriate features W queue contains messages") {
    // Given two registered features Alpha and Bravo, with initial tracking consent:
    // - Alpha (feature ID "ALFA"): NotGranted
    // - Bravo (feature ID "BRVO"): Granted
    impl::MockFilesystem fs;
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    std::vector<RegisteredFeature> features;
    init_features(
        TrackingConsent::NotGranted, TrackingConsent::Granted, fs, clock, features
    );
    REQUIRE(features.size() == 2);

    // And a queue to which the following messages have been produced:
    // - Handle write from "ALFA" (should be ignored: NotGranted)
    // - Handle write from "BRVO" (should be written to v1/)
    // - Change tracking consent to Pending for all features
    // - Handle write from "ALFA" (should be written to intermediate-v1/)
    // - Handle write from "BRVO" (should be written to intermediate-v1/)
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

    // When we run the storage thread and drain the queue
    queue.Stop();
    StorageThreadMain(DiagnosticLogger{}, queue, features);

    // Then alpha/intermediate-v1 should contain 'alpha-1'
    auto alpha_pending = fs.FindFiles(EVENTS_ROOT + "/alpha/intermediate-v1");
    REQUIRE(alpha_pending.size() == 1);
    REQUIRE(
        fs.Cat(EVENTS_ROOT + "/alpha/intermediate-v1/" + alpha_pending.front()) ==
        MockTLVFile().AppendEvent("alpha-1").ToString()
    );

    // And alpha/v1 should be empty (alpha-0 was dropped due to NotGranted)
    REQUIRE(fs.FindFiles(EVENTS_ROOT + "/alpha/v1").empty());

    // And bravo/intermediate-v1 should contain 'bravo-1'
    auto bravo_pending = fs.FindFiles(EVENTS_ROOT + "/bravo/intermediate-v1");
    REQUIRE(bravo_pending.size() == 1);
    REQUIRE(
        fs.Cat(EVENTS_ROOT + "/bravo/intermediate-v1/" + bravo_pending.front()) ==
        MockTLVFile().AppendEvent("bravo-1").ToString()
    );

    // And bravo/v1 should contain 'bravo-0'
    auto bravo_granted = fs.FindFiles(EVENTS_ROOT + "/bravo/v1");
    REQUIRE(bravo_granted.size() == 1);
    REQUIRE(
        fs.Cat(EVENTS_ROOT + "/bravo/v1/" + bravo_granted.front()) ==
        MockTLVFile().AppendEvent("bravo-0").ToString()
    );
  }
}
