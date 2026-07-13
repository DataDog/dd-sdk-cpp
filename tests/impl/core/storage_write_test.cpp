// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/storage_write.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "datadog/impl/core/storage/feature_event.hpp"
#include "datadog/impl/core/storage/sdk.hpp"

#include "mock/clock.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("BatchWriter", "[unit]") {
  // Given a system clock frozen at a known timestamp
  MockClock clock;
  clock.FreezeAtMilliseconds(1700000000000);

  // And a diagnostic logger that will capture all messages emitted
  DiagnosticMessageBuffer diagnostics;
  DiagnosticLogger logger = diagnostics.CreateTestLogger();

  // And a mock filesystem with a fully initialized FeatureEventStorage interface that
  // will manage app/.datadog/main/555/foo
  MockFilesystem fs;
  fs.Mkdirs("app");
  SdkStorage sdk_storage(fs, logger, 555);
  REQUIRE(sdk_storage.Initialize("app", "main"));
  auto storage = sdk_storage.InitializeFeatureEventStorage("foo");
  REQUIRE(storage != nullptr);

  const std::string pending_dir_path = "app/.datadog/main/555/foo/intermediate-v1";
  const std::string pending_prefix = pending_dir_path + "/";
  const std::string granted_dir_path = "app/.datadog/main/555/foo/v1";
  const std::string granted_prefix = granted_dir_path + "/";

  // And a function that we can use to initialize a BatchWriter with a certain config
  // and initial state
  auto init_writer = [&](const BatchWriterConfig config,
                         TrackingConsent initial_consent = TrackingConsent::Pending) {
    BatchWriter writer(logger, fs, *storage, clock, config);
    writer.SetTrackingConsent(initial_consent);
    return writer;
  };

  SECTION("M successfully write event W valid event data provided") {
    // Given a BatchWriter configured to write to 'intermediate-v1/'
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config);

    // When we write event data, Then our writes are successful
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    REQUIRE(writer.HandleWrite("event-1", "metadata-1", false));
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // And our event storage directory contains a single batch file with all of our
    // event data serialized to it, TLV-encoded
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 1);
    std::string expected = MockTLVFile()
                               .AppendEvent("event-0")
                               .AppendMetadata("metadata-1")
                               .AppendEvent("event-1")
                               .AppendEvent("event-2")
                               .ToString();
    REQUIRE(fs.Cat(pending_prefix + names.front()) == expected);

    // And the name of that file matches the system time when it was created
    REQUIRE(names.front() == "1700000000000");
  }

  SECTION("M handle successive writes in the same file W limits are not met") {
    // Given a batch writer with reasonable default values, and a start time of
    // 1700000000000
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(config);

    // When we write a succession of 50 small events, with 16ms elapsing between
    // each event (for a total duration of 0.8 seconds)
    MockTLVFile want(1024);
    for (int i = 0; i < 50; i++) {
      std::string event_metadata = "m-" + std::to_string(i);
      std::string event = "e-" + std::to_string(i);
      want.AppendMetadata(event_metadata);
      want.AppendEvent(event);
      writer.HandleWrite(event, event_metadata, false);
      clock.TickMilliseconds(16);
    }

    // Then all of those events are contained in a single batch file
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 1);
    REQUIRE(names.front() == "1700000000000");
    REQUIRE(fs.Cat(pending_prefix + names.front()) == want.ToString());
  }

  SECTION("M start new file W max file age exceeded {BatchSize::Small ~ 3s}") {
    // Given a batch writer configured with BatchSize::Small, and a start time of
    // 1700000000000
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config);

    // When we write an event
    REQUIRE(writer.HandleWrite("event-0", {}, false));

    // And then write another event one second later
    clock.TickMilliseconds(1000);
    REQUIRE(writer.HandleWrite("event-1", {}, false));

    // And then wait three seconds before writing another event
    clock.TickMilliseconds(3000);
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then our event storage directory contains two batches
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);

    // And the first file contains the first two events and has a name matching our
    // start timestamp
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // And the second file contains the last event and has a name matches the time
    // four seconds later
    REQUIRE(names[1] == "1700000004000");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M start new file W max file age exceeded {BatchSize::Medium ~ 10s}") {
    // Given a batch writer configured with BatchSize::Medium, and a start time of
    // 1700000000000
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(config);

    // When we events at T+0:00, then T+0:09, then T+0:10
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(9000);
    REQUIRE(writer.HandleWrite("event-1", {}, false));
    clock.TickMilliseconds(1000);
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then our event storage directory contains two batches
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);

    // And the first file matches our start time and has the first two events
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // And the second file matches our end time and has the last event
    REQUIRE(names[1] == "1700000010000");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M start new file W max file age exceeded {BatchSize::Large ~ 35s}") {
    // Given a batch writer configured with BatchSize::Large, and a start time of
    // 1700000000000
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    BatchWriter writer = init_writer(config);

    // When we events at T+0:00, then T+0:33, then T+0:35
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(33000);
    REQUIRE(writer.HandleWrite("event-1", {}, false));
    clock.TickMilliseconds(2000);
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then our event storage directory contains two batches
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);

    // And the first file matches our start time and has the first two events
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // And the second file matches our end time and has the last event
    REQUIRE(names[1] == "1700000035000");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M start new file W max file size exceeded") {
    // Given a batch writer configured with a max batch file size of 64 bytes
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 64;
    BatchWriter writer = init_writer(config);

    // When we write four payloads that are exactly 20 bytes each (incl. 6-byte
    // header), with an interval of 10ms between writes
    REQUIRE(writer.HandleWrite("twenty-bytes-0", {}, false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-1", {}, false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-2", {}, false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-3", {}, false));

    // Then our event storage directory contains two batches
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);

    // And the first file matches our start time and has the first three events
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) == MockTLVFile()
                                                 .AppendEvent("twenty-bytes-0")
                                                 .AppendEvent("twenty-bytes-1")
                                                 .AppendEvent("twenty-bytes-2")
                                                 .ToString()
    );

    // And the second file matches our end time and has the last event
    REQUIRE(names[1] == "1700000000030");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendEvent("twenty-bytes-3").ToString()
    );
  }

  SECTION("M consider both event and metadata W computing size impact of write") {
    // Given a batch writer configured with a max batch file size of 64 bytes
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 64;
    BatchWriter writer = init_writer(config);

    // When we write two 40-byte payloads, separated by 10ms
    REQUIRE(writer.HandleWrite("twenty-bytes-0", "twenty-bytes-a", false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-1", "twenty-bytes-b", false));

    // Then our event storage directory contains two batches
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);

    // And the first file matches our start time and has the event and metadata
    // blocks for our first event
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) == MockTLVFile()
                                                 .AppendMetadata("twenty-bytes-a")
                                                 .AppendEvent("twenty-bytes-0")
                                                 .ToString()
    );

    // And the second file matches our end time and has the event and metadata
    // blocks for the second event
    REQUIRE(names[1] == "1700000000010");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) == MockTLVFile()
                                                 .AppendMetadata("twenty-bytes-b")
                                                 .AppendEvent("twenty-bytes-1")
                                                 .ToString()
    );
  }

  SECTION("M write to same file W file size would exactly match max file size") {
    // Given a batch writer configured with a max batch file size of 40 bytes
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 40;
    BatchWriter writer = init_writer(config);

    // When we write two 20-byte payloads
    REQUIRE(writer.HandleWrite("twenty-bytes-0", {}, false));
    REQUIRE(writer.HandleWrite("twenty-bytes-1", {}, false));

    // Then they end up in the same file
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) == MockTLVFile()
                                                 .AppendEvent("twenty-bytes-0")
                                                 .AppendEvent("twenty-bytes-1")
                                                 .ToString()
    );
  }

  SECTION("M start new file W new file size would exceed max file size by 1 byte") {
    // Given a batch writer configured with a max batch file size of 39 bytes
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 39;
    BatchWriter writer = init_writer(config);

    // When we write two 20-byte payloads
    REQUIRE(writer.HandleWrite("twenty-bytes-0", {}, false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-1", {}, false));

    // Then they end up in separate files
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) ==
        MockTLVFile().AppendEvent("twenty-bytes-0").ToString()
    );
    REQUIRE(names[1] == "1700000000010");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendEvent("twenty-bytes-1").ToString()
    );
  }

  SECTION("M start new file W max writes per file exceeded") {
    // Given a batch writer configured to write no more than 2 events per file
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = 2;
    BatchWriter writer = init_writer(config);

    // When we write three events, 10ms apart
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-1", {}, false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then they're split into two files
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );
    REQUIRE(names[1] == "1700000000020");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M consider both event and metadata W evaluating max writes per file") {
    // Given a batch writer configured to write no more than 2 events per file
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = 2;
    BatchWriter writer = init_writer(config);

    // When we write three events, 10ms apart
    REQUIRE(writer.HandleWrite("event-0", "metadata-0", false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1", false));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-2", "metadata-2", false));

    // Then they're split into two files just the same
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(
        fs.Cat(pending_prefix + names[0]) == MockTLVFile()
                                                 .AppendMetadata("metadata-0")
                                                 .AppendEvent("event-0")
                                                 .AppendMetadata("metadata-1")
                                                 .AppendEvent("event-1")
                                                 .ToString()
    );
    REQUIRE(names[1] == "1700000000020");
    REQUIRE(
        fs.Cat(pending_prefix + names[1]) ==
        MockTLVFile().AppendMetadata("metadata-2").AppendEvent("event-2").ToString()
    );
  }

  SECTION("M drop event W max writes per file is zero") {
    // Given a batch writer configured to write no more than 0 events per file
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = 0;
    BatchWriter writer = init_writer(config);

    // When we attempt to write a payload of any size, at any time
    const bool write_ok = writer.HandleWrite("event-0", {}, false);

    // Then the event is not accepted
    REQUIRE(write_ok == false);

    // And nothing is written to disk
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    REQUIRE(names.empty());
  }

  SECTION("M drop event W max writes per file is negative") {
    // Given a batch writer configured to write no more than -1 events per file
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = -1;
    BatchWriter writer = init_writer(config);

    // When we attempt to write a payload of any size, at any time
    const bool write_ok = writer.HandleWrite("event-0", {}, false);

    // Then the event is not accepted
    REQUIRE(write_ok == false);

    // And nothing is written to disk
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    REQUIRE(names.empty());
  }

  SECTION("M find not-in-use filename W chosen name conflicts with existing file") {
    // Given an ordinary BatchWriter at start time 1700000000000
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(config);

    // And an event directory that already contains files named '1700000000000',
    // '1700000000001', and '1700000000002'
    fs.Touch(pending_prefix + "1700000000000", "non");
    fs.Touch(pending_prefix + "1700000000001", "net");
    fs.Touch(pending_prefix + "1700000000002", "nie");
    fs.Touch(pending_prefix + "1700000000004", "nope");

    // When we write an event, Then it succeeds
    REQUIRE(writer.HandleWrite("event-0", {}, false));

    // And the newly-written event is stored in a file whose name reflects the next
    // available timestamp value
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 5);
    REQUIRE(names[3] == "1700000000003");
    REQUIRE(
        fs.Cat(pending_prefix + names[3]) ==
        MockTLVFile().AppendEvent("event-0").ToString()
    );
  }

  SECTION("M drop event W all potential filenames are already in use") {
    // Given an ordinary BatchWriter at start time 1700000000000
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(config);

    // And an event directory that already contains a file named with every possible
    // millisecond-precision timestamp value between now and 100ms from now
    for (uint64_t i = 0; i < 100; i++) {
      const uint64_t timestamp_ms = 1700000000000 + i;
      const std::string path = pending_prefix + std::to_string(timestamp_ms);
      fs.Touch(path, "no dice");
    }

    // When we attempt to write an event
    bool write_ok = writer.HandleWrite("event-0", {}, false);

    // Then the event is rejected
    REQUIRE(write_ok == false);

    // And no files are modified
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 100);
    const std::string expected = MockTLVFile().AppendBytes("no dice").ToString();
    for (const std::string& name : names) {
      REQUIRE(fs.Cat(pending_prefix + name) == expected);
    }

    // But: When some time passes and we attempt another write
    clock.TickMilliseconds(100);
    write_ok = writer.HandleWrite("event-1", {}, false);

    // Then the event is accepted
    REQUIRE(write_ok == true);

    // And a new batch file is created
    names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 101);
    REQUIRE(std::find(names.begin(), names.end(), "1700000000100") != names.end());
  }

  SECTION("M remove all files in pending W tracking consent changes to NotGranted") {
    // Given a BatchWriter configured to write to 'intermediate-v1/'
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1", false));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then we have two batch files in our directory, as expected
    auto names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent has been revoked
    const bool ok = writer.SetTrackingConsent(TrackingConsent::NotGranted);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory is now empty
    names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 0);

    // And our granted directory is empty as ever
    names = fs.Ls(granted_dir_path);
    REQUIRE(names.size() == 0);

    // And if we subsequently write an event, it is dropped rather than being flushed to
    // storage, since tracking consent is explicitly revoked
    REQUIRE(writer.HandleWrite("event-3", {}, false));
    REQUIRE(fs.Ls(pending_dir_path).size() == 0);
    REQUIRE(fs.Ls(granted_dir_path).size() == 0);
  }

  SECTION(
      "M move all files from pending to granted W tracking consent changes to Granted"
  ) {
    // Given a BatchWriter configured to write to 'intermediate-v1/'
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1", false));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then we have two batch files in our directory, as expected
    auto names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is now granted
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Granted);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory is now empty
    names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 0);

    // And our granted directory now contains the two batches that were previously
    // written to pending
    names = fs.Ls(granted_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // And if we subsequently write an event, it's flushed to storage directly to the
    // 'granted' directory
    REQUIRE(writer.HandleWrite("event-3", {}, false));
    REQUIRE(fs.Ls(pending_dir_path).size() == 0);
    names = fs.Ls(granted_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // And we've create a brand new batch file to contain our new data; we don't
    // continue writing to the old file post-move
    REQUIRE(names[2] == "1700000060101");
  }

  SECTION(
      "M handle conflict by deleting file from pending W both pending and granted "
      "contain a file by that name"
  ) {
    // Given a 'v1/' directory that already contains a file named '1700000000000'
    fs.Touch(granted_prefix + "1700000000000", "existing-contents");

    // And a BatchWriter configured to write to 'intermediate-v1/'
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1", false));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then we have two batch files in our directory, as expected
    auto names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is now granted
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Granted);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory is now empty
    names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 0);

    // And our granted directory now contains the two batches that were previously
    // written to pending
    names = fs.Ls(granted_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // But v1/1700000000000 contains the original, unmodified contents that were
    // present initially, while v1/1700000060100 has been newly moved
    REQUIRE(fs.Cat(granted_prefix + "1700000000000") == "existing-contents");
    REQUIRE(
        fs.Cat(granted_prefix + "1700000060100") ==
        MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M fail W unable to move files from pending dir to granted dir") {
    // Given a BatchWriter and an event in 'intermediate-v1/'
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config);
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(100);

    // And a 'v1/' directory that can not be written to
    fs.Touch(granted_prefix + ".dummy", "ensures-that-granted-dir-exists");
    fs.SimulateFailure(granted_dir_path, FilesystemResult::UnknownError);

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is now granted
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Granted);

    // Then the operation fails
    REQUIRE(!ok);

    // And our pending directory retains its original contents, unmodified
    REQUIRE(fs.Ls(pending_dir_path).size() == 1);
    REQUIRE(
        fs.Cat(pending_prefix + "1700000000000") ==
        MockTLVFile().AppendEvent("event-0").ToString()
    );

    // And our granted directory contains no event batches
    auto names = fs.Ls(granted_dir_path);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == ".dummy");
  }

  SECTION(
      "M move no files but begin writing to pending dir W tracking consent changes to "
      "Pending"
  ) {
    // Given a batch writer configured to write to v1/
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config, TrackingConsent::Granted);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}, false));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1", false));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then we have two batch files in our directory, as expected
    auto names = fs.Ls(granted_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is once again pending
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Pending);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory remains empty
    names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 0);

    // And our granted directory still contains the two batches that were placed there
    // while consent was granted
    names = fs.Ls(granted_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000060100");

    // And if we subsequently write an event, it's flushed to the 'pending' directory
    clock.TickMilliseconds(5);
    REQUIRE(writer.HandleWrite("event-3", {}, false));
    REQUIRE(fs.Ls(granted_dir_path).size() == 2);
    names = fs.Ls(pending_dir_path);
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "1700000060105");
  }

  SECTION(
      "M write to granted dir W bypass_tracking_consent is true and consent is Pending"
  ) {
    // Given a batch writer with consent Pending
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config, TrackingConsent::Pending);

    // When we write with bypass_tracking_consent = true
    const bool bypass_tracking_consent = true;
    REQUIRE(writer.HandleWrite("event-0", {}, bypass_tracking_consent));

    // Then the event is written to the granted directory, not pending
    REQUIRE(fs.Ls(pending_dir_path).size() == 0);
    auto names = fs.Ls(granted_dir_path);
    REQUIRE(names.size() == 1);
  }

  SECTION(
      "M write to granted dir W bypass_tracking_consent is true and consent is "
      "NotGranted"
  ) {
    // Given a batch writer with consent NotGranted (which would normally drop the
    // event)
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config, TrackingConsent::NotGranted);

    // When we write with bypass_tracking_consent = true
    const bool bypass_tracking_consent = true;
    REQUIRE(writer.HandleWrite("event-0", {}, bypass_tracking_consent));

    // Then the event is written to the granted directory despite revoked consent
    REQUIRE(fs.Ls(pending_dir_path).size() == 0);
    auto names = fs.Ls(granted_dir_path);
    REQUIRE(names.size() == 1);
  }

  SECTION("M drop event W single write exceeds max_event_size but not max_file_size") {
    // Given a batch writer with a generous max_file_size but a tight max_event_size
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 1024 * 1024;  // 1 MB batch file — plenty of room
    config.max_event_size = 20;          // only 20 bytes per event
    BatchWriter writer = init_writer(config);

    // When we attempt to write a payload where header (6 bytes) + data (15 bytes) = 21
    REQUIRE_FALSE(writer.HandleWrite("fifteen-bytes!!", {}, false));

    // Then nothing is written to disk
    REQUIRE(fs.Ls(pending_dir_path).empty());
  }

  SECTION("M accept event W single write exactly equals max_event_size") {
    // Given a batch writer with max_event_size set to exactly fit a 14-byte write
    // (6-byte TLV header + 8 bytes data)
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_event_size = 14;
    config.max_file_size = 1024 * 1024;
    BatchWriter writer = init_writer(config);

    // When we write an 8-byte event (total encoded size = 14 bytes)
    REQUIRE(writer.HandleWrite("8-bytes!", {}, false));
    REQUIRE(fs.Ls(pending_dir_path).size() == 1);
  }

  SECTION("M purge oldest files W directory size exceeds max_directory_size") {
    // Given a batch writer with a directory quota of 50 bytes
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_directory_size = 50;
    BatchWriter writer = init_writer(config);

    // And three existing batch files each 40 bytes at timestamps 0, 1, and 2
    // (total 120 bytes, well over the 50-byte quota)
    fs.Touch(pending_prefix + "1700000000000", std::string(40, 'x'));
    fs.Touch(pending_prefix + "1700000000001", std::string(40, 'y'));
    fs.Touch(pending_prefix + "1700000000002", std::string(40, 'z'));

    // When we write a new event 100ms later (triggers new file + quota check)
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-new", {}, false));

    // Then the two oldest files are purged (oldest: 40 bytes → 80 > 50; next: 40
    // bytes → 40 ≤ 50, stop), leaving only the youngest pre-existing file and the
    // new batch file
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000002");
    REQUIRE(names[1] == "1700000000100");
  }

  SECTION("M not purge any files W directory size is within max_directory_size") {
    // Given a batch writer with a generous quota
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_directory_size = 10000;
    BatchWriter writer = init_writer(config);

    // And a small existing file in the pending directory
    fs.Touch(pending_prefix + "1700000000000", std::string(10, 'x'));

    // When we write a new event after triggering a file rotation
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-new", {}, false));

    // Then no files are deleted
    std::vector<std::string> names = fs.Ls(pending_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000000");
    REQUIRE(names[1] == "1700000000100");
  }

  SECTION("M not purge non-numeric-named files W directory contains foreign files") {
    // Given a batch writer with a very small quota
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_directory_size = 1;
    BatchWriter writer = init_writer(config);

    // And a non-numerically-named file (not written by the SDK) that would push the
    // directory over quota if counted
    fs.Touch(pending_prefix + "not-a-batch", std::string(100, 'x'));

    // When a new event triggers the quota check
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-new", {}, false));

    // Then the non-SDK file is left untouched
    REQUIRE(fs.IsFile(pending_prefix + "not-a-batch"));
  }

  SECTION(
      "M leave existing files intact W directory listing fails on new file creation"
  ) {
    // Given a batch writer with a directory quota so tight that any existing file
    // would trigger a purge
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_directory_size = 1;
    BatchWriter writer = init_writer(config);

    // And an existing batch file in the pending directory
    fs.Touch(pending_prefix + "1700000000000", std::string(100, 'x'));

    // And a filesystem that refuses to list the pending directory
    fs.SimulateFailure(
        pending_dir_path,
        FilesystemResult::PermissionDenied,
        MockFilesystem::FailureFlags::Ls
    );

    // When we attempt to write an event (no active file, so new-file path is taken and
    // the quota check fires). The write also fails because filename resolution requires
    // listing the same directory.
    clock.TickMilliseconds(100);
    writer.HandleWrite("event-new", {}, false);

    // Then the pre-existing file is untouched — purge bailed out rather than
    // deleting files based on an incomplete view of the directory
    fs.ClearSimulatedFailure(pending_dir_path);
    REQUIRE(fs.IsFile(pending_prefix + "1700000000000"));
  }

  SECTION(
      "M drop event W metadata and event together exceed max_event_size but neither "
      "alone would"
  ) {
    // Given a batch writer where max_event_size = 24 bytes.
    // A 5-byte event alone encodes to 11 bytes (6-byte TLV header + 5 data), which is
    // within the limit. An 8-byte metadata block encodes to 14 bytes. Combined they
    // require 25 bytes, which exceeds the 24-byte cap.
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 1024 * 1024;
    config.max_event_size = 24;
    BatchWriter writer = init_writer(config);

    // When we write the 5-byte event with no metadata, it fits (11 bytes ≤ 24)
    REQUIRE(writer.HandleWrite("event", {}, false));

    // When we write the same event with an 8-byte metadata block, the combined size
    // of 25 bytes exceeds the per-event cap
    REQUIRE_FALSE(writer.HandleWrite("event", "metadata", false));

    // Then only the first write produced a file; the second was dropped
    REQUIRE(fs.Ls(pending_dir_path).size() == 1);
  }

  SECTION("M purge oldest files W quota exceeded in granted directory") {
    // Given a batch writer writing to the granted directory with a 50-byte quota
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_directory_size = 50;
    BatchWriter writer = init_writer(config, TrackingConsent::Granted);

    // And three existing files of 40 bytes each (total 120 bytes, over the 50-byte
    // quota)
    fs.Touch(granted_prefix + "1700000000000", std::string(40, 'x'));
    fs.Touch(granted_prefix + "1700000000001", std::string(40, 'y'));
    fs.Touch(granted_prefix + "1700000000002", std::string(40, 'z'));

    // When we write a new event 100ms later (triggers new file + quota check)
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-new", {}, false));

    // Then the two oldest files are purged, leaving only the youngest pre-existing
    // file and the new batch file
    std::vector<std::string> names = fs.Ls(granted_dir_path);
    std::sort(names.begin(), names.end());
    REQUIRE(names.size() == 2);
    REQUIRE(names[0] == "1700000000002");
    REQUIRE(names[1] == "1700000000100");
  }

  SECTION("M treat GetFileSize failure as zero W file stat fails during quota check") {
    // Given a batch writer with a 50-byte quota
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_directory_size = 50;
    BatchWriter writer = init_writer(config);

    // And an oldest file that is 40 bytes but has a simulated stat failure, so it
    // contributes 0 bytes to the quota total
    fs.Touch(pending_prefix + "1700000000000", std::string(40, 'x'));
    fs.SimulateFailure(
        pending_prefix + "1700000000000",
        FilesystemResult::UnknownError,
        MockFilesystem::FailureFlags::Stat
    );

    // And two younger files of 10 bytes each (true total 60 bytes, but apparent total
    // with the stat failure counted as 0 is only 20 bytes — within the 50-byte quota)
    fs.Touch(pending_prefix + "1700000000001", std::string(10, 'y'));
    fs.Touch(pending_prefix + "1700000000002", std::string(10, 'z'));

    // When we write a new event 100ms later (triggers new file + quota check)
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-new", {}, false));

    // Then no files are purged: the failed stat caused the directory to appear to be
    // within quota, so the purge was a no-op
    REQUIRE(fs.IsFile(pending_prefix + "1700000000000"));
    REQUIRE(fs.IsFile(pending_prefix + "1700000000001"));
    REQUIRE(fs.IsFile(pending_prefix + "1700000000002"));
  }

  SECTION("M write bypass event to granted dir W active file is warm in pending dir") {
    // Given a batch writer with consent Pending and a prior normal write that has
    // warmed up _active_file to a file in the pending directory
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(config, TrackingConsent::Pending);
    REQUIRE(writer.HandleWrite("event-0", {}, false));

    // And a small clock advance that keeps _active_file within its reuse window
    // (BatchSize::Small max_file_age is ~2850ms)
    clock.TickMilliseconds(10);

    // When we write a second event with bypass_tracking_consent = true
    const bool bypass_tracking_consent = true;
    REQUIRE(writer.HandleWrite("event-1", {}, bypass_tracking_consent));

    // Then event-0 is in the pending directory
    REQUIRE(fs.Ls(pending_dir_path).size() == 1);
    REQUIRE(
        fs.Cat(pending_dir_path + "/1700000000000") ==
        MockTLVFile().AppendEvent("event-0").ToString()
    );

    // And event-1 is in the granted directory, not appended to the pending file
    REQUIRE(fs.Ls(granted_dir_path).size() == 1);
    REQUIRE(
        fs.Cat(granted_dir_path + "/1700000000010") ==
        MockTLVFile().AppendEvent("event-1").ToString()
    );

    // Next: When we write a _third_ event that _doesn't_ bypass tracking consent, after
    // another very brief delay
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-2", {}, false));

    // Then our original pending-consent batch file now contains "event-0,event-2": we
    // don't forget about our existing (and still-valid-for-reuse) batch file just
    // because we wrote an event to the consent-granted directory
    REQUIRE(fs.Ls(pending_dir_path).size() == 1);
    REQUIRE(
        fs.Cat(pending_dir_path + "/1700000000000") ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-2").ToString()
    );
  }
}
