// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2024-Present Datadog, Inc.

#include "core/upload.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cinttypes>
#include <memory>
#include <thread>
#include <utility>

#include "core/context.hpp"
#include "core/core.hpp"
#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "mock/tlv.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("UploadThreadState", "[unit]") {
  // Test all valid upload frequency values
  std::array<UploadFrequency, 3> frequencies{
      UploadFrequency::Frequent, UploadFrequency::Average, UploadFrequency::Rare
  };
  for (const UploadFrequency frequency : frequencies) {
    // When we create UploadThreadState with the configured frequency
    UploadThreadState state(frequency);

    // Then the delay values should be set
    REQUIRE(state.min_delay > platform::Duration{0});
    REQUIRE(state.current_delay > platform::Duration{0});
    REQUIRE(state.max_delay > platform::Duration{0});

    // And max should be not be less than min
    REQUIRE(state.max_delay >= state.min_delay);

    // And initial delay should be somewhere in the middle
    const platform::Duration initial_delay = state.current_delay;
    REQUIRE(initial_delay > state.min_delay);
    REQUIRE(initial_delay < state.max_delay);

    // And incrementing the delay should increase it toward max
    const platform::Duration increased_delay = state.IncreaseDelayTowardMax();
    REQUIRE(increased_delay == state.current_delay);
    REQUIRE(increased_delay > initial_delay);
    REQUIRE(increased_delay < state.max_delay);

    // And resetting the delay should instantly jump it to min
    const platform::Duration reset_delay = state.ResetDelayToMin();
    REQUIRE(reset_delay == state.min_delay);

    // And incrementing it many times should eventually clamp it to the max
    for (int i = 0; i < 500; i++) {
      state.IncreaseDelayTowardMax();
    }
    REQUIRE(state.current_delay == state.max_delay);
  }
}

TEST_CASE("UploadScheduler", "[unit]") {
  SECTION("M schedule and return feature W single feature scheduled") {
    // Given a scheduler with an upload cycle scheduled in 1 millisecond
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::milliseconds(1));

    // When we wait for the next scheduled feature
    auto started_at = std::chrono::high_resolution_clock::now();
    auto result = scheduler.WaitForNext();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then we get the ID of the feature we scheduled
    REQUIRE(result.has_value());
    REQUIRE(*result == 0xfeee0000);

    // And somewhere between 0.5ms and 100ms has elapsed
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() >= 1);
    REQUIRE(elapsed_ms.count() <= 100);
  }

  SECTION("M return earliest feature W features scheduled at different times") {
    // Given a scheduler where 0xfeee0000 is scheduled in 20 microseconds
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::microseconds(20));

    // And 0x1337beef is scheduled in 10 microseconds
    scheduler.Schedule(0x1337beef, std::chrono::microseconds(10));

    // When we wait for the next scheduled feature
    auto result = scheduler.WaitForNext();

    // Then we get 0x1337beef
    REQUIRE(result.has_value());
    REQUIRE(*result == 0x1337beef);

    // And: when we wait for the next feature after that
    result = scheduler.WaitForNext();

    // Then we get feee0000
    REQUIRE(result.has_value());
    REQUIRE(*result == 0xfeee0000);
  }

  SECTION("M return nullopt W no features scheduled") {
    // Given a scheduler with no features scheduled
    MockClock clock;
    UploadScheduler scheduler(clock);

    // When we wait for the next scheduled feature
    auto started_at = std::chrono::high_resolution_clock::now();
    auto result = scheduler.WaitForNext();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then we get no value
    REQUIRE(!result.has_value());

    // And no blocking wait has occurred
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() == 0);
  }

  SECTION("M return immediately W scheduled time has already passed") {
    // Given a feature scheduled to run an upload cycle in 9 seconds
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::seconds(9));

    // When 9 seconds or more has elapsed
    clock.FreezeAt(clock.Now());
    clock.Tick(std::chrono::seconds(10));

    // And we wait for the next scheduled feature
    auto started_at = std::chrono::high_resolution_clock::now();
    auto result = scheduler.WaitForNext();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then we get our feature
    REQUIRE(result.has_value());
    REQUIRE(*result == 0xfeee0000);

    // And no blocking wait has occurred
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() == 0);
  }

  SECTION("M return nullopt W Stop called during wait") {
    // Given a feature scheduled to run an upload cycle in 10 seconds
    MockClock clock;
    UploadScheduler scheduler(clock);
    scheduler.Schedule(0xfeee0000, std::chrono::seconds(10));

    // And a thread that's waiting for that 10-second delay to elapse
    std::atomic<int> num_elapsed{0};
    auto block_until_nullopt = [&]() {
      while (auto next = scheduler.WaitForNext()) {
        num_elapsed++;
      }
    };
    std::thread thread{block_until_nullopt};
    std::this_thread::sleep_for(std::chrono::microseconds(10));

    // When we stop scheduling and join on the thread
    auto started_at = std::chrono::high_resolution_clock::now();
    scheduler.Stop();
    thread.join();
    auto elapsed = std::chrono::high_resolution_clock::now() - started_at;

    // Then the thread exits immediately
    auto elapsed_ms = std::chrono::round<std::chrono::milliseconds>(elapsed);
    REQUIRE(elapsed_ms.count() == 0);

    // And the scheduled upload cycle does not take place
    REQUIRE(num_elapsed == 0);
  }
}

TEST_CASE("HandleUploadProc", "[unit]") {
  auto init_config = [](
                         BatchSize batch_size, UploadFrequency upload_frequency,
                         BatchProcessingLevel batch_processing_level
                     ) -> std::tuple<CoreConfig, UploadThreadConfig, CoreContext> {
    CoreConfig config{
        TrackingConsent::Granted,
        Site::us1,
        "mock-client-token",
        "mock-service",
        "mock-env",
        "mock-application-version",
        batch_size,
        upload_frequency,
        batch_processing_level,
        0,
        ""
    };
    return std::make_tuple(
        config,
        UploadThreadConfig::FromCoreConfig(
            config.batch_size, config.batch_processing_level
        ),
        CoreContext(config)
    );
  };

  auto register_feature = [](const CoreConfig& config,
                             MockStorageDirectory& mock_storage,
                             const std::shared_ptr<Feature>& feature,
                             std::vector<RegisteredFeature>& out_features) -> void {
    // Prepare a storage directory with a subdirectory for granted events, which is
    // the only directory the upload thread knows about
    auto directory = mock_storage.PrepareSubdirectory(feature->GetName());
    REQUIRE(directory.has_value());
    auto granted_subdir = (*directory)->PrepareSubdirectory("yes-upload");
    REQUIRE(granted_subdir.has_value());

    // "Register" the feature
    out_features.emplace_back(
        feature->GetId(), feature->GetName(), feature, std::move(*directory),
        nullptr,  // event_storage is exclusive to storage thread
        std::move(*granted_subdir),
        std::make_unique<UploadThreadState>(config.upload_frequency)
    );
  };

  SECTION("M take no action W no batch files are present") {
    // Given a single registered feature with no event data present
    MockClock clock;
    MockHttpClient client;
    MockStorageDirectory storage;
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then no calls are made to UploadThread_PrepareReport
    REQUIRE(alpha->reports.empty());

    // And no HTTP requests are made
    REQUIRE(client.requests.empty());

    // And our storage directory remains as empty as it ever was
    REQUIRE(storage.FindFiles("alpha/yes-upload").empty());

    // And our next cycle for the feature is scheduled with the default delay
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(50));
  }

  SECTION("M process available batch files W batch files are present") {
    // Given a single registered feature
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    MockStorageDirectory storage;
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And a batch of event data that was written a minute ago
    storage.WithExistingFile(
        "alpha/yes-upload/1699999940000",
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then one call is made to UploadThread_PrepareReport
    REQUIRE(alpha->reports.size() == 1);
    const MockReport& report = alpha->reports[0];
    REQUIRE(report.blocks_read.size() == 2);
    REQUIRE(report.blocks_read[0].type == TLVBlockType::Event);
    REQUIRE(report.blocks_read[0].data == Block{"event-0"});
    REQUIRE(report.blocks_read[1].type == TLVBlockType::Event);
    REQUIRE(report.blocks_read[1].data == Block{"event-1"});
    REQUIRE(!report.last_read_error.has_value());

    // And one HTTP request is made as a result
    REQUIRE(client.requests.size() == 1);
    const MockHttpRequest& request = client.requests[0];
    REQUIRE(
        request.url == "https://browser-intake-datadoghq.com/api/v1/test?ddsource=unity"
    );
    REQUIRE(
        request.headers.find(
            "Content-Type: text/plain\n"
            "DD-API-KEY: mock-client-token\n"
            "DD-EVP-ORIGIN: unity\n"
            "DD-EVP-ORIGIN-VERSION: 0.2.0\n"
        ) == 0
    );
    REQUIRE(request.body == "event-0,event-1");
    REQUIRE(request.aborted == false);

    // And our storage directory is now empty, because the batch was deleted
    REQUIRE(storage.FindFiles("alpha/yes-upload").empty());

    // And our next cycle for the feature is scheduled with the minimum delay,
    // because we successfully uploaded a batch
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
  }

  SECTION(
      "M upload multiple batches W multiple batch files are eligible"
      " {respecting min file age for read}"
  ) {
    // Given a directory with a series of batch files of different ages
    auto init_files = [](MockStorageDirectory& storage) {
      // Test begins at 1700000000000ms
      storage.WithExistingFile(
          "alpha/yes-upload/1699999955000",
          MockTLVFile().AppendEvent("created-45s-ago").ToString()
      );
      storage.WithExistingFile(
          "alpha/yes-upload/1699999985000",
          MockTLVFile().AppendEvent("created-15s-ago").ToString()
      );
      storage.WithExistingFile(
          "alpha/yes-upload/1699999995000",
          MockTLVFile().AppendEvent("created-5s-ago").ToString()
      );
      storage.WithExistingFile(
          "alpha/yes-upload/1699999999000",
          MockTLVFile().AppendEvent("created-1s-ago").ToString()
      );
    };

    // And a variety of BatchSize values, which should affect our cutoff for
    // considering a file too young to read
    struct TestParams {
      std::string_view name;
      BatchSize batch_size;
      std::vector<std::string> want_request_bodies;
      std::vector<std::string> want_remaining_batch_files;
    };
    std::vector<TestParams> tests = {
        {"{BatchSize::Small ~ 3s}",
         BatchSize::Small,
         {"created-45s-ago", "created-15s-ago", "created-5s-ago"},
         {"alpha/yes-upload/1699999999000"}},

        {"{BatchSize::Medium ~ 10s}",
         BatchSize::Medium,
         {"created-45s-ago", "created-15s-ago"},
         {"alpha/yes-upload/1699999995000", "alpha/yes-upload/1699999999000"}},

        {"{BatchSize::Large ~ 35s}",
         BatchSize::Large,
         {"created-45s-ago"},
         {"alpha/yes-upload/1699999985000", "alpha/yes-upload/1699999995000",
          "alpha/yes-upload/1699999999000"}},
    };

    for (const auto& tt : tests) {
      DYNAMIC_SECTION(tt.name) {
        // Given a single registered feature
        MockClock clock;
        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        MockStorageDirectory storage;
        const auto [core_config, config, context] = init_config(
            tt.batch_size, UploadFrequency::Average, BatchProcessingLevel::Medium
        );
        auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
        std::vector<RegisteredFeature> features;
        register_feature(core_config, storage, alpha, features);

        // And our set of four batch files
        init_files(storage);

        // When we process uploads for that feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
            config, context, clock, CreateFeatureId("ALFA"), features, client,
            filenames, read_buffer
        );

        // Then a call is made to UploadThread_PrepareReport for each batch
        REQUIRE(alpha->reports.size() == tt.want_request_bodies.size());

        // And an HTTP request is initiated for each batch
        REQUIRE(client.requests.size() == tt.want_request_bodies.size());
        for (size_t i = 0; i < tt.want_request_bodies.size(); i++) {
          REQUIRE(client.requests[i].body == tt.want_request_bodies[i]);
        }

        // And only the files that were too new for upload remain on disk
        auto relpaths = storage.FindFiles("alpha/yes-upload");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths == tt.want_remaining_batch_files);

        // And our delay is reduced to minimum due to successful cycle
        REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
      }
    }
  }

  SECTION(
      "M ignore and delete batches W batch files exceed max age for read"
      " {respecting min file age for read}"
  ) {
    // Given a single registered feature
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;

    MockStorageDirectory storage;
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And a set of three batch files: one older than 18 hours, one eligible for
    // upload, and one too new to upload
    storage.WithExistingFile(
        "alpha/yes-upload/1699827200000",
        MockTLVFile().AppendEvent("created-48h-ago").ToString()
    );
    storage.WithExistingFile(
        "alpha/yes-upload/1699999985000",
        MockTLVFile().AppendEvent("created-15s-ago").ToString()
    );
    storage.WithExistingFile(
        "alpha/yes-upload/1699999999000",
        MockTLVFile().AppendEvent("created-1s-ago").ToString()
    );

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then one batch is successfully processed and uploaded: the middle one
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "created-15s-ago");

    // And only the file that was too new for upload remains on disk: both the
    // too-old-to-upload batch and the successfully-uploaded batch are deleted
    auto relpaths = storage.FindFiles("alpha/yes-upload");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "alpha/yes-upload/1699999999000");

    // And our delay is reduced to minimum due to successful cycle
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
  }

  SECTION(
      "M return time to next cycle W cycle is successful"
      " {respecting upload frequency}"
  ) {
    // Given a range of UploadFrequency values, which should affect the interval
    // between upload cycles for a given feature
    struct TestParams {
      std::string_view name;
      UploadFrequency upload_frequency;
      int64_t want_num_seconds;
    };
    std::vector<TestParams> tests = {
        {"{UploadFrequency::Frequent ~ 3s}", UploadFrequency::Frequent, 3},
        {"{UploadFrequency::Average ~ 10s}", UploadFrequency::Average, 10},
        {"{UploadFrequency::Rare ~ 35s}", UploadFrequency::Rare, 35},
    };
    for (const auto& tt : tests) {
      DYNAMIC_SECTION(tt.name) {
        // Given a single registered feature with a single batch of event data
        MockClock clock;
        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        MockStorageDirectory storage;
        storage.WithExistingFile(
            "alpha/yes-upload/1699999955000",
            MockTLVFile().AppendEvent("event").ToString()
        );
        const auto [core_config, config, context] = init_config(
            BatchSize::Medium, tt.upload_frequency, BatchProcessingLevel::Medium
        );
        auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
        std::vector<RegisteredFeature> features;
        register_feature(core_config, storage, alpha, features);

        // When we process uploads for that feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
            config, context, clock, CreateFeatureId("ALFA"), features, client,
            filenames, read_buffer
        );

        // Then our single batch is processed and uploaded
        REQUIRE(alpha->reports.size() == 1);
        REQUIRE(client.requests.size() == 1);
        REQUIRE(storage.FindFiles("alpha/yes-upload").empty());

        // And our delay is reduced to the appropriate minimum interval for our
        // given batch size
        REQUIRE(delay_until_next_cycle == std::chrono::seconds(tt.want_num_seconds));
      }
    }
  }

  SECTION(
      "M return time to next cycle W cycle is unsuccessful"
      " {respecting upload frequency}"
  ) {
    // Given a range of UploadFrequency values, which should affect the interval
    // between upload cycles for a given feature
    struct TestParams {
      std::string_view name;
      UploadFrequency upload_frequency;
      int64_t want_num_milliseconds;
    };
    std::vector<TestParams> tests = {
        // Min 3s; initial 15s; after one failed attempt: 15s * 1.1 => 16.5s
        {"{UploadFrequency::Frequent ~ 3s}", UploadFrequency::Frequent, 16500},
        // Min 10s; initial 50s; after one failed attempt: 50s * 1.1 => 55s
        {"{UploadFrequency::Average ~ 10s}", UploadFrequency::Average, 55000},
        // Min 35s; initial 175s; after one failed attempt: 175s * 1.1 => 192.5s
        {"{UploadFrequency::Rare ~ 35s}", UploadFrequency::Rare, 192500},
    };
    for (const auto& tt : tests) {
      DYNAMIC_SECTION(tt.name) {
        // Given a single registered feature with a single batch of event data
        MockClock clock;
        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        MockStorageDirectory storage;
        storage.WithExistingFile(
            "alpha/yes-upload/1699999955000",
            MockTLVFile().AppendEvent("event").ToString()
        );
        const auto [core_config, config, context] = init_config(
            BatchSize::Medium, tt.upload_frequency, BatchProcessingLevel::Medium
        );
        auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
        std::vector<RegisteredFeature> features;
        register_feature(core_config, storage, alpha, features);

        // And an HTTP client that is unable to complete requests
        client.SimulateTransientNetworkError();

        // When we process uploads for our feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
            config, context, clock, CreateFeatureId("ALFA"), features, client,
            filenames, read_buffer
        );

        // Then our single batch is processed but not successfully uploaded, and
        // so remains on disk
        REQUIRE(alpha->reports.size() == 1);
        REQUIRE(client.requests.size() == 1);
        REQUIRE(storage.FindFiles("alpha/yes-upload").size() == 1);

        // And our delay is reduced to the appropriate minimum interval for our
        // given batch size
        REQUIRE(
            std::chrono::round<std::chrono::milliseconds>(delay_until_next_cycle) ==
            std::chrono::milliseconds(tt.want_num_milliseconds)
        );
      }
    }
  }

  SECTION(
      "M upload multiple batches W multiple batch files are eligible"
      " {respecting max batches per cycle}"
  ) {
    // Given a directory with 101 batch files, all of which were created long enough
    // ago to be eligible for upload
    auto init_files = [](MockStorageDirectory& storage) {
      // Test begins at 1700000000000ms
      const std::string contents = MockTLVFile().AppendEvent("event").ToString();
      for (int i = 0; i < 101; i++) {
        const std::string filename = std::to_string(1699999955000 + i);
        const std::string relpath = "alpha/yes-upload/" + filename;
        storage.WithExistingFile(relpath, contents);
      }
    };

    // And a variety of BatchProcessingLevel values, which should limit the number
    // of batch files we'll process in a single upload cycle
    struct TestParams {
      std::string_view name;
      BatchProcessingLevel batch_processing_level;
      size_t want_num_processed;
    };
    std::vector<TestParams> tests = {
        {"{BatchProcessingLevel::Low <= 1}", BatchProcessingLevel::Low, 1},
        {"{BatchProcessingLevel::Medium <= 20}", BatchProcessingLevel::Medium, 20},
        {"{BatchSize::High <= 100}", BatchProcessingLevel::High, 100},
    };
    for (const auto& tt : tests) {
      DYNAMIC_SECTION(tt.name) {
        // Given a single registered feature
        MockClock clock;
        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        MockStorageDirectory storage;
        const auto [core_config, config, context] = init_config(
            BatchSize::Medium, UploadFrequency::Average, tt.batch_processing_level
        );
        auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
        std::vector<RegisteredFeature> features;
        register_feature(core_config, storage, alpha, features);

        // And our set of 101 batch files
        init_files(storage);

        // When we process uploads for that feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
            config, context, clock, CreateFeatureId("ALFA"), features, client,
            filenames, read_buffer
        );

        // Then a call is made to UploadThread_PrepareReport for each batch
        REQUIRE(alpha->reports.size() == tt.want_num_processed);

        // And an HTTP request is initiated for each batch
        REQUIRE(client.requests.size() == tt.want_num_processed);

        // And only the files that did not make the cutoff remain on disk, with
        // older batches deleted and newer batches remaining
        std::vector<std::string> want_relpaths;
        for (size_t i = tt.want_num_processed; i < 101; i++) {
          const std::string filename = std::to_string(1699999955000 + i);
          want_relpaths.emplace_back("alpha/yes-upload/" + filename);
        }
        auto relpaths = storage.FindFiles("alpha/yes-upload");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths == want_relpaths);

        // And our delay is reduced to minimum due to successful cycle
        REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
      }
    }
  }

  SECTION("M take no action W unable to list files in directory") {
    // Given a single registered feature with a single batch of event data
    MockClock clock;
    MockHttpClient client;
    MockStorageDirectory storage;
    storage.WithExistingFile(
        "alpha/yes-upload/1699999955000", MockTLVFile().AppendEvent("event").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And a filesystem that will not permit the SDK to read directory contents
    storage.SetFail("alpha/yes-upload", true);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then no batches are processed or uploaded
    REQUIRE(alpha->reports.empty());
    REQUIRE(client.requests.empty());

    // And our event data remains on disk
    auto relpaths = storage.FindFiles("alpha/yes-upload");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "alpha/yes-upload/1699999955000");

    // And our initial delay remains unmodified
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(50));
  }

  SECTION("M retain batch and halt processing W unable to open batch file for read") {
    // Given a single registered feature with three eligible batches
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    MockStorageDirectory storage;
    storage.WithExistingFile(
        "alpha/yes-upload/1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    storage.WithExistingFile(
        "alpha/yes-upload/1699999956000",
        MockTLVFile().AppendEvent("event-1").ToString()
    );
    storage.WithExistingFile(
        "alpha/yes-upload/1699999957000",
        MockTLVFile().AppendEvent("event-2").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And a filesystem that will prevent the second file from being read
    storage.SetFail("alpha/yes-upload/1699999956000", true);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then only our first batch is uploaded
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "event-0");

    // And our other two batches remain on disk
    auto relpaths = storage.FindFiles("alpha/yes-upload");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "alpha/yes-upload/1699999956000");
    REQUIRE(relpaths[1] == "alpha/yes-upload/1699999957000");

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }

  SECTION("M reject batch and continue processing W unable to process batch") {
    // Given a single registered feature with three eligible batches, the second of
    // which does not contain valid TLV data
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    MockStorageDirectory storage;
    storage.WithExistingFile(
        "alpha/yes-upload/1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    storage.WithExistingFile("alpha/yes-upload/1699999956000", "invalid-tlv-format");
    storage.WithExistingFile(
        "alpha/yes-upload/1699999957000",
        MockTLVFile().AppendEvent("event-2").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then our first and third batches are uploaded
    REQUIRE(alpha->reports.size() == 2);
    REQUIRE(client.requests.size() == 2);
    REQUIRE(client.requests[0].body == "event-0");
    REQUIRE(client.requests[1].body == "event-2");

    // And no batches remain on disk, as both the successfully-uploaded batches and
    // the malformed batch were deleted
    REQUIRE(storage.FindFiles("alpha/yes-upload").empty());

    // And this feature's upload delay is reduced because the cycle finished with a
    // successful upload
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
  }

  SECTION("M retain batch and halt processing W upload fails due to network error") {
    // Given a single registered feature with a single eligible batch
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    MockStorageDirectory storage;
    storage.WithExistingFile(
        "alpha/yes-upload/1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And an HTTP client that will fail to complete requests
    client.SimulateTransientNetworkError();

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then the first batch is processed, and an upload is attempted
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "event-0");

    // But the file remains on disk, as upload was not successful
    auto relpaths = storage.FindFiles("alpha/yes-upload");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "alpha/yes-upload/1699999955000");

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }

  SECTION("M retain batch and halt processing W upload gets HTTP server error") {
    // Given a single registered feature with a single eligible batch
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    MockStorageDirectory storage;
    storage.WithExistingFile(
        "alpha/yes-upload/1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And an HTTP client that will get a 502 response
    client.SimulateResponse(502);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then the first batch is processed, and an upload is attempted
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "event-0");

    // But the file remains on disk, as upload was not successful
    auto relpaths = storage.FindFiles("alpha/yes-upload");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "alpha/yes-upload/1699999955000");

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }

  SECTION("M reject batch and continue processing W upload gets HTTP client error") {
    // Given a single registered feature with two eligible batches that can be read
    // and processed without error
    MockClock clock;
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    MockStorageDirectory storage;
    storage.WithExistingFile(
        "alpha/yes-upload/1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    storage.WithExistingFile(
        "alpha/yes-upload/1699999956000",
        MockTLVFile().AppendEvent("event-1").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");
    std::vector<RegisteredFeature> features;
    register_feature(core_config, storage, alpha, features);

    // And an HTTP client that will get a 400 response
    client.SimulateResponse(400);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    platform::Duration delay_until_next_cycle = Internal_HandleUploadProc(
        config, context, clock, CreateFeatureId("ALFA"), features, client, filenames,
        read_buffer
    );

    // Then both batches are processed and uploaded
    REQUIRE(alpha->reports.size() == 2);
    REQUIRE(client.requests.size() == 2);
    REQUIRE(client.requests[0].body == "event-0");
    REQUIRE(client.requests[1].body == "event-1");

    // And both batches are deleted, as they were rejected by the server
    REQUIRE(storage.FindFiles("alpha/yes-upload").empty());

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }
}
