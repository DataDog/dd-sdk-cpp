// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/upload_thread.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <memory>
#include <thread>
#include <utility>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/core.hpp"
#include "datadog/impl/storage/sdk.hpp"

#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/http_client.hpp"
#include "mock/tlv.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("UploadThreadState", "[unit]") {
  // Given any of our three valid UploadFrequency values
  auto frequency = GENERATE(
      UploadFrequency::Frequent, UploadFrequency::Average, UploadFrequency::Rare
  );

  // When we create UploadThreadState with the configured frequency
  UploadThreadState state(frequency);

  // Then the delay values should be set
  REQUIRE(state.min_delay > Duration{0});
  REQUIRE(state.current_delay > Duration{0});
  REQUIRE(state.max_delay > Duration{0});

  // And max should be not be less than min
  REQUIRE(state.max_delay >= state.min_delay);

  // And initial delay should be somewhere in the middle
  const Duration initial_delay = state.current_delay;
  REQUIRE(initial_delay > state.min_delay);
  REQUIRE(initial_delay < state.max_delay);

  // And incrementing the delay should increase it toward max
  const Duration increased_delay = state.IncreaseDelayTowardMax();
  REQUIRE(increased_delay == state.current_delay);
  REQUIRE(increased_delay > initial_delay);
  REQUIRE(increased_delay < state.max_delay);

  // And resetting the delay should instantly jump it to min
  const Duration reset_delay = state.ResetDelayToMin();
  REQUIRE(reset_delay == state.min_delay);

  // And incrementing it many times should eventually clamp it to the max
  for (int i = 0; i < 500; i++) {
    state.IncreaseDelayTowardMax();
  }
  REQUIRE(state.current_delay == state.max_delay);
}

TEST_CASE("HandleUploadProc", "[unit]") {
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

  // And a mock feature implementation that we can inspect to see what upload-thread
  // requests were initiated
  auto alpha = std::make_shared<MockFeature>(CreateFeatureId("ALFA"), "alpha");

  // And a FeatureEventStorage interface for that feature
  auto alpha_events = storage.InitializeFeatureEventStorage("alpha");
  REQUIRE(alpha_events != nullptr);
  const std::string alpha_granted_dir = "app/.datadog/main/12345/alpha/v1";
  const std::string alpha_granted_prefix = alpha_granted_dir + "/";

  // And a function that will initialize a set of common config objects based on our
  // desired SDK configuration parameters
  auto init_config = [](BatchSize batch_size,
                        UploadFrequency upload_frequency,
                        BatchProcessingLevel batch_processing_level)
      -> std::
          tuple<CoreConfig, UploadThreadConfig, std::shared_ptr<const HttpContext>> {
            CoreConfig config =
                CoreConfig("mock-client-token", "mock-service", "mock-env")
                    .SetInitialTrackingConsent(TrackingConsent::Granted)
                    .SetApplicationVersion("mock-application-version")
                    .SetBatchSize(batch_size)
                    .SetUploadFrequency(upload_frequency)
                    .SetBatchProcessingLevel(batch_processing_level);
            return std::make_tuple(
                config,
                UploadThreadConfig::FromCoreConfig(batch_size, batch_processing_level),
                std::make_shared<HttpContext>(config)
            );
          };

  // And a function that will initialize a vector containing a single RegisteredFeature
  // entry that contains appropriate UploadThreadState for our mock 'alpha' feature
  auto register_feature =
      [&](UploadFrequency upload_frequency) -> std::vector<RegisteredFeature> {
    std::vector<RegisteredFeature> features;
    features.emplace_back(
        alpha->GetId(),
        alpha->GetName(),
        std::move(alpha_events),
        alpha,
        nullptr,  // batch_writer is exclusive to storage thread
        std::make_unique<UploadThreadState>(upload_frequency)
    );
    return features;
  };

  SECTION("M take no action W no batch files are present") {
    // Given a single registered feature with no event data present
    MockHttpClient client;
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then no calls are made to UploadThread_PrepareReport
    REQUIRE(alpha->reports.empty());

    // And no HTTP requests are made
    REQUIRE(client.requests.empty());

    // And our storage directory remains as empty as it ever was
    REQUIRE(fs.Ls(alpha_granted_dir).empty());

    // And our next cycle for the feature is scheduled with the default delay
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(50));
  }

  SECTION("M process available batch files W batch files are present") {
    // Given a single registered feature
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And a batch of event data that was written a minute ago
    fs.Touch(
        alpha_granted_prefix + "1699999940000",
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
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
    REQUIRE(report.last_read_status.has_value());
    REQUIRE(report.last_read_status.value() == BatchReader::Result::Status::EndOfFile);

    // And one HTTP request is made as a result
    REQUIRE(client.requests.size() == 1);
    const MockHttpRequest& request = client.requests[0];
    REQUIRE(
        request.url ==
        "https://browser-intake-datadoghq.com/api/v1/test?ddsource=rum-cpp"
    );
    REQUIRE(
        request.headers.find(
            "Content-Type: text/plain\n"
            "DD-API-KEY: mock-client-token\n"
            "DD-EVP-ORIGIN: rum-cpp\n"
            "DD-EVP-ORIGIN-VERSION: 0.2.0\n"
        ) == 0
    );
    REQUIRE(request.body == "event-0,event-1");
    REQUIRE(request.aborted == false);

    // And our storage directory is now empty, because the batch was deleted
    REQUIRE(fs.Ls(alpha_granted_dir).empty());

    // And our next cycle for the feature is scheduled with the minimum delay,
    // because we successfully uploaded a batch
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
  }

  SECTION(
      "M upload multiple batches W multiple batch files are eligible"
      " {respecting min file age for read}"
  ) {
    // Given a directory with a series of batch files of different ages
    auto init_files = [&]() {
      // Test begins at 1700000000000ms
      fs.Touch(
          alpha_granted_prefix + "1699999955000",
          MockTLVFile().AppendEvent("created-45s-ago").ToString()
      );
      fs.Touch(
          alpha_granted_prefix + "1699999985000",
          MockTLVFile().AppendEvent("created-15s-ago").ToString()
      );
      fs.Touch(
          alpha_granted_prefix + "1699999995000",
          MockTLVFile().AppendEvent("created-5s-ago").ToString()
      );
      fs.Touch(
          alpha_granted_prefix + "1699999999000",
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
         {"1699999999000"}},

        {"{BatchSize::Medium ~ 10s}",
         BatchSize::Medium,
         {"created-45s-ago", "created-15s-ago"},
         {"1699999995000", "1699999999000"}},

        {"{BatchSize::Large ~ 35s}",
         BatchSize::Large,
         {"created-45s-ago"},
         {"1699999985000", "1699999995000", "1699999999000"}},
    };

    for (const auto& tt : tests) {
      DYNAMIC_SECTION(tt.name) {
        // Given a single registered feature

        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        const auto [core_config, config, context] = init_config(
            tt.batch_size, UploadFrequency::Average, BatchProcessingLevel::Medium
        );
        auto features = register_feature(UploadFrequency::Average);

        // And our set of four batch files
        init_files();

        // When we process uploads for that feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        Duration delay_until_next_cycle = Internal_HandleUploadProc(
            logger,
            config,
            *context,
            clock,
            CreateFeatureId("ALFA"),
            features,
            fs,
            client,
            filenames,
            read_buffer
        );

        // Then a call is made to UploadThread_PrepareReport for each batch
        REQUIRE(alpha->reports.size() == tt.want_request_bodies.size());

        // And an HTTP request is initiated for each batch
        REQUIRE(client.requests.size() == tt.want_request_bodies.size());
        for (size_t i = 0; i < tt.want_request_bodies.size(); i++) {
          REQUIRE(client.requests[i].body == tt.want_request_bodies[i]);
        }

        // And only the files that were too new for upload remain on disk
        auto names = fs.Ls(alpha_granted_dir);
        std::sort(names.begin(), names.end());
        REQUIRE(names == tt.want_remaining_batch_files);

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
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;

    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And a set of three batch files: one older than 18 hours, one eligible for
    // upload, and one too new to upload
    fs.Touch(
        alpha_granted_prefix + "1699827200000",
        MockTLVFile().AppendEvent("created-48h-ago").ToString()
    );
    fs.Touch(
        alpha_granted_prefix + "1699999985000",
        MockTLVFile().AppendEvent("created-15s-ago").ToString()
    );
    fs.Touch(
        alpha_granted_prefix + "1699999999000",
        MockTLVFile().AppendEvent("created-1s-ago").ToString()
    );

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then one batch is successfully processed and uploaded: the middle one
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "created-15s-ago");

    // And only the file that was too new for upload remains on disk: both the
    // too-old-to-upload batch and the successfully-uploaded batch are deleted
    auto names = fs.Ls(alpha_granted_dir);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "1699999999000");

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

        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        fs.Touch(
            alpha_granted_prefix + "1699999955000",
            MockTLVFile().AppendEvent("event").ToString()
        );
        const auto [core_config, config, context] = init_config(
            BatchSize::Medium, tt.upload_frequency, BatchProcessingLevel::Medium
        );
        auto features = register_feature(tt.upload_frequency);

        // When we process uploads for that feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        Duration delay_until_next_cycle = Internal_HandleUploadProc(
            logger,
            config,
            *context,
            clock,
            CreateFeatureId("ALFA"),
            features,
            fs,
            client,
            filenames,
            read_buffer
        );

        // Then our single batch is processed and uploaded
        REQUIRE(alpha->reports.size() == 1);
        REQUIRE(client.requests.size() == 1);
        REQUIRE(fs.Ls(alpha_granted_dir).empty());

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

        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        fs.Touch(
            alpha_granted_prefix + "1699999955000",
            MockTLVFile().AppendEvent("event").ToString()
        );
        const auto [core_config, config, context] = init_config(
            BatchSize::Medium, tt.upload_frequency, BatchProcessingLevel::Medium
        );
        auto features = register_feature(tt.upload_frequency);

        // And an HTTP client that is unable to complete requests
        client.SimulateTransientNetworkError();

        // When we process uploads for our feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        Duration delay_until_next_cycle = Internal_HandleUploadProc(
            logger,
            config,
            *context,
            clock,
            CreateFeatureId("ALFA"),
            features,
            fs,
            client,
            filenames,
            read_buffer
        );

        // Then our single batch is processed but not successfully uploaded, and
        // so remains on disk
        REQUIRE(alpha->reports.size() == 1);
        REQUIRE(client.requests.size() == 1);
        REQUIRE(fs.Ls(alpha_granted_dir).size() == 1);

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
    auto init_files = [&]() {
      // Test begins at 1700000000000ms
      const std::string contents = MockTLVFile().AppendEvent("event").ToString();
      for (int i = 0; i < 101; i++) {
        const std::string filename = std::to_string(1699999955000 + i);
        fs.Touch(alpha_granted_prefix + filename, contents);
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

        clock.FreezeAtMilliseconds(1700000000000);
        MockHttpClient client;
        const auto [core_config, config, context] = init_config(
            BatchSize::Medium, UploadFrequency::Average, tt.batch_processing_level
        );
        auto features = register_feature(UploadFrequency::Average);

        // And our set of 101 batch files
        init_files();

        // When we process uploads for that feature
        std::vector<std::string> filenames;
        std::vector<char> read_buffer;
        Duration delay_until_next_cycle = Internal_HandleUploadProc(
            logger,
            config,
            *context,
            clock,
            CreateFeatureId("ALFA"),
            features,
            fs,
            client,
            filenames,
            read_buffer
        );

        // Then a call is made to UploadThread_PrepareReport for each batch
        REQUIRE(alpha->reports.size() == tt.want_num_processed);

        // And an HTTP request is initiated for each batch
        REQUIRE(client.requests.size() == tt.want_num_processed);

        // And only the files that did not make the cutoff remain on disk, with
        // older batches deleted and newer batches remaining
        std::vector<std::string> want_names;
        for (size_t i = tt.want_num_processed; i < 101; i++) {
          const std::string filename = std::to_string(1699999955000 + i);
          want_names.emplace_back(filename);
        }
        auto names = fs.Ls(alpha_granted_dir);
        std::sort(names.begin(), names.end());
        REQUIRE(names == want_names);

        // And our delay is reduced to minimum due to successful cycle
        REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
      }
    }
  }

  SECTION("M take no action W unable to list files in directory") {
    // Given a single registered feature with a single batch of event data
    MockHttpClient client;
    fs.Touch(
        alpha_granted_prefix + "1699999955000",
        MockTLVFile().AppendEvent("event").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And a filesystem that will not permit the SDK to read directory contents
    fs.SimulateFailure(alpha_granted_dir, FilesystemResult::UnknownError);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then no batches are processed or uploaded
    REQUIRE(alpha->reports.empty());
    REQUIRE(client.requests.empty());

    // And our event data remains on disk
    auto names = fs.Ls(alpha_granted_dir);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "1699999955000");

    // And our initial delay remains unmodified
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(50));
  }

  SECTION("M retain batch and halt processing W unable to open batch file for read") {
    // Given a single registered feature with three eligible batches
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    fs.Touch(
        alpha_granted_prefix + "1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    fs.Touch(
        alpha_granted_prefix + "1699999956000",
        MockTLVFile().AppendEvent("event-1").ToString()
    );
    fs.Touch(
        alpha_granted_prefix + "1699999957000",
        MockTLVFile().AppendEvent("event-2").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And a filesystem that will prevent the second file from being read
    fs.SimulateFailure(
        alpha_granted_prefix + "1699999956000", FilesystemResult::UnknownError
    );

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then only our first batch is uploaded
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "event-0");

    // And our other two batches remain on disk
    auto names = fs.Ls(alpha_granted_dir);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1699999956000");
    REQUIRE(names[1] == "1699999957000");

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }

  SECTION("M reject batch and continue processing W unable to process batch") {
    // Given a single registered feature with three eligible batches, the second of
    // which does not contain valid TLV data
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    fs.Touch(
        alpha_granted_prefix + "1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    fs.Touch(alpha_granted_prefix + "1699999956000", "invalid-tlv-format");
    fs.Touch(
        alpha_granted_prefix + "1699999957000",
        MockTLVFile().AppendEvent("event-2").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then our first and third batches are uploaded
    REQUIRE(alpha->reports.size() == 2);
    REQUIRE(client.requests.size() == 2);
    REQUIRE(client.requests[0].body == "event-0");
    REQUIRE(client.requests[1].body == "event-2");

    // And no batches remain on disk, as both the successfully-uploaded batches and
    // the malformed batch were deleted
    REQUIRE(fs.Ls(alpha_granted_dir).empty());

    // And this feature's upload delay is reduced because the cycle finished with a
    // successful upload
    REQUIRE(delay_until_next_cycle == std::chrono::seconds(10));
  }

  SECTION("M retain batch and halt processing W upload fails due to network error") {
    // Given a single registered feature with a single eligible batch
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    fs.Touch(
        alpha_granted_prefix + "1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And an HTTP client that will fail to complete requests
    client.SimulateTransientNetworkError();

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then the first batch is processed, and an upload is attempted
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "event-0");

    // But the file remains on disk, as upload was not successful
    auto names = fs.Ls(alpha_granted_dir);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "1699999955000");

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }

  SECTION("M retain batch and halt processing W upload gets HTTP server error") {
    // Given a single registered feature with a single eligible batch
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    fs.Touch(
        alpha_granted_prefix + "1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And an HTTP client that will get a 502 response
    client.SimulateResponse(502);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then the first batch is processed, and an upload is attempted
    REQUIRE(alpha->reports.size() == 1);
    REQUIRE(client.requests.size() == 1);
    REQUIRE(client.requests[0].body == "event-0");

    // But the file remains on disk, as upload was not successful
    auto names = fs.Ls(alpha_granted_dir);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "1699999955000");

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }

  SECTION("M reject batch and continue processing W upload gets HTTP client error") {
    // Given a single registered feature with two eligible batches that can be read
    // and processed without error
    clock.FreezeAtMilliseconds(1700000000000);
    MockHttpClient client;
    fs.Touch(
        alpha_granted_prefix + "1699999955000",
        MockTLVFile().AppendEvent("event-0").ToString()
    );
    fs.Touch(
        alpha_granted_prefix + "1699999956000",
        MockTLVFile().AppendEvent("event-1").ToString()
    );
    const auto [core_config, config, context] = init_config(
        BatchSize::Medium, UploadFrequency::Average, BatchProcessingLevel::Medium
    );
    auto features = register_feature(UploadFrequency::Average);

    // And an HTTP client that will get a 400 response
    client.SimulateResponse(400);

    // When we process uploads for that feature
    std::vector<std::string> filenames;
    std::vector<char> read_buffer;
    Duration delay_until_next_cycle = Internal_HandleUploadProc(
        logger,
        config,
        *context,
        clock,
        CreateFeatureId("ALFA"),
        features,
        fs,
        client,
        filenames,
        read_buffer
    );

    // Then both batches are processed and uploaded
    REQUIRE(alpha->reports.size() == 2);
    REQUIRE(client.requests.size() == 2);
    REQUIRE(client.requests[0].body == "event-0");
    REQUIRE(client.requests[1].body == "event-1");

    // And both batches are deleted, as they were rejected by the server
    REQUIRE(fs.Ls(alpha_granted_dir).empty());

    // And this feature's upload delay is increased due to failure
    REQUIRE(delay_until_next_cycle == std::chrono::milliseconds(55000));
  }
}
