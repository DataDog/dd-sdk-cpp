// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "core/storage_write.hpp"

#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "mock/clock.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("BatchWriter", "[unit]") {
  auto init_writer = [](MockClock& clock,
                        MockStorageDirectory& storage,
                        const BatchWriterConfig config,
                        TrackingConsent initial_consent = TrackingConsent::Pending) {
    // Set the system clock to a known timestamp
    clock.FreezeAtMilliseconds(1700000000000);

    // Prepare a mock directory to contain events
    auto pending_directory = storage.PrepareSubdirectory("pending");
    auto granted_directory = storage.PrepareSubdirectory("granted");
    REQUIRE(pending_directory.has_value());
    REQUIRE(granted_directory.has_value());

    // Return an initialized writer
    return BatchWriter(
        DiagnosticLogger{},
        initial_consent,
        std::move(*pending_directory),
        std::move(*granted_directory),
        clock,
        config
    );
  };

  SECTION("M successfully write event W valid event data provided") {
    // Given a BatchWriter configured to write to 'pending/'
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write event data, Then our writes are successful
    REQUIRE(writer.HandleWrite("event-0", {}));
    REQUIRE(writer.HandleWrite("event-1", "metadata-1"));
    REQUIRE(writer.HandleWrite("event-2", {}));

    // And our event storage directory contains a single batch file with all of our
    // event data serialized to it, TLV-encoded
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 1);
    std::string expected = MockTLVFile()
                               .AppendEvent("event-0")
                               .AppendMetadata("metadata-1")
                               .AppendEvent("event-1")
                               .AppendEvent("event-2")
                               .ToString();
    REQUIRE(storage.Cat(relpaths.front()) == expected);

    // And the name of that file matches the system time when it was created
    REQUIRE(relpaths.front() == "pending/1700000000000");
  }

  SECTION("M handle successive writes in the same file W limits are not met") {
    // Given a batch writer with reasonable default values, and a start time of
    // 1700000000000
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write a succession of 50 small events, with 16ms elapsing between
    // each event (for a total duration of 0.8 seconds)
    MockTLVFile want(1024);
    for (int i = 0; i < 50; i++) {
      std::string event_metadata = "m-" + std::to_string(i);
      std::string event = "e-" + std::to_string(i);
      want.AppendMetadata(event_metadata);
      want.AppendEvent(event);
      writer.HandleWrite(event, event_metadata);
      clock.TickMilliseconds(16);
    }

    // Then all of those events are contained in a single batch file
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths.front() == "pending/1700000000000");
    REQUIRE(storage.Cat(relpaths.front()) == want.ToString());
  }

  SECTION("M start new file W max file age exceeded {BatchSize::Small ~ 3s}") {
    // Given a batch writer configured with BatchSize::Small, and a start time of
    // 1700000000000
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write an event
    REQUIRE(writer.HandleWrite("event-0", {}));

    // And then write another event one second later
    clock.TickMilliseconds(1000);
    REQUIRE(writer.HandleWrite("event-1", {}));

    // And then wait three seconds before writing another event
    clock.TickMilliseconds(3000);
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then our event storage directory contains two batches
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);

    // And the first file contains the first two events and has a name matching our
    // start timestamp
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // And the second file contains the last event and has a name matches the time
    // four seconds later
    REQUIRE(relpaths[1] == "pending/1700000004000");
    REQUIRE(
        storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M start new file W max file age exceeded {BatchSize::Medium ~ 10s}") {
    // Given a batch writer configured with BatchSize::Medium, and a start time of
    // 1700000000000
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(clock, storage, config);

    // When we events at T+0:00, then T+0:09, then T+0:10
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(9000);
    REQUIRE(writer.HandleWrite("event-1", {}));
    clock.TickMilliseconds(1000);
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then our event storage directory contains two batches
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);

    // And the first file matches our start time and has the first two events
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // And the second file matches our end time and has the last event
    REQUIRE(relpaths[1] == "pending/1700000010000");
    REQUIRE(
        storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M start new file W max file age exceeded {BatchSize::Large ~ 35s}") {
    // Given a batch writer configured with BatchSize::Large, and a start time of
    // 1700000000000
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    BatchWriter writer = init_writer(clock, storage, config);

    // When we events at T+0:00, then T+0:33, then T+0:35
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(33000);
    REQUIRE(writer.HandleWrite("event-1", {}));
    clock.TickMilliseconds(2000);
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then our event storage directory contains two batches
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);

    // And the first file matches our start time and has the first two events
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );

    // And the second file matches our end time and has the last event
    REQUIRE(relpaths[1] == "pending/1700000035000");
    REQUIRE(
        storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M start new file W max file size exceeded") {
    // Given a batch writer configured with a max batch file size of 64 bytes
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 64;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write four payloads that are exactly 20 bytes each (incl. 6-byte
    // header), with an interval of 10ms between writes
    REQUIRE(writer.HandleWrite("twenty-bytes-0", {}));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-1", {}));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-2", {}));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-3", {}));

    // Then our event storage directory contains two batches
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);

    // And the first file matches our start time and has the first three events
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) == MockTLVFile()
                                        .AppendEvent("twenty-bytes-0")
                                        .AppendEvent("twenty-bytes-1")
                                        .AppendEvent("twenty-bytes-2")
                                        .ToString()
    );

    // And the second file matches our end time and has the last event
    REQUIRE(relpaths[1] == "pending/1700000000030");
    REQUIRE(
        storage.Cat(relpaths[1]) ==
        MockTLVFile().AppendEvent("twenty-bytes-3").ToString()
    );
  }

  SECTION("M consider both event and metadata W computing size impact of write") {
    // Given a batch writer configured with a max batch file size of 64 bytes
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 64;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write two 40-byte payloads, separated by 10ms
    REQUIRE(writer.HandleWrite("twenty-bytes-0", "twenty-bytes-a"));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-1", "twenty-bytes-b"));

    // Then our event storage directory contains two batches
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);

    // And the first file matches our start time and has the event and metadata
    // blocks for our first event
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) == MockTLVFile()
                                        .AppendMetadata("twenty-bytes-a")
                                        .AppendEvent("twenty-bytes-0")
                                        .ToString()
    );

    // And the second file matches our end time and has the event and metadata
    // blocks for the second event
    REQUIRE(relpaths[1] == "pending/1700000000010");
    REQUIRE(
        storage.Cat(relpaths[1]) == MockTLVFile()
                                        .AppendMetadata("twenty-bytes-b")
                                        .AppendEvent("twenty-bytes-1")
                                        .ToString()
    );
  }

  SECTION("M write to same file W file size would exactly match max file size") {
    // Given a batch writer configured with a max batch file size of 40 bytes
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 40;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write two 20-byte payloads
    REQUIRE(writer.HandleWrite("twenty-bytes-0", {}));
    REQUIRE(writer.HandleWrite("twenty-bytes-1", {}));

    // Then they end up in the same file
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) == MockTLVFile()
                                        .AppendEvent("twenty-bytes-0")
                                        .AppendEvent("twenty-bytes-1")
                                        .ToString()
    );
  }

  SECTION("M start new file W new file size would exceed max file size by 1 byte") {
    // Given a batch writer configured with a max batch file size of 39 bytes
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 39;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write two 20-byte payloads
    REQUIRE(writer.HandleWrite("twenty-bytes-0", {}));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("twenty-bytes-1", {}));

    // Then they end up in separate files
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) ==
        MockTLVFile().AppendEvent("twenty-bytes-0").ToString()
    );
    REQUIRE(relpaths[1] == "pending/1700000000010");
    REQUIRE(
        storage.Cat(relpaths[1]) ==
        MockTLVFile().AppendEvent("twenty-bytes-1").ToString()
    );
  }

  SECTION("M drop event W single write exceeds max file size") {
    // Given a batch writer configured with a max batch file size of 16 bytes
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_file_size = 16;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we attempt to write a 20-byte payload, which is larger than any file
    // could contain
    const bool write_ok = writer.HandleWrite("twenty-bytes-0", {});

    // Then the event is not accepted
    REQUIRE(write_ok == false);

    // And nothing is written to disk
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.empty());
  }

  SECTION("M start new file W max writes per file exceeded") {
    // Given a batch writer configured to write no more than 2 events per file
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = 2;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write three events, 10ms apart
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-1", {}));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then they're split into two files
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) ==
        MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
    );
    REQUIRE(relpaths[1] == "pending/1700000000020");
    REQUIRE(
        storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M consider both event and metadata W evaluating max writes per file") {
    // Given a batch writer configured to write no more than 2 events per file
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = 2;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we write three events, 10ms apart
    REQUIRE(writer.HandleWrite("event-0", "metadata-0"));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1"));
    clock.TickMilliseconds(10);
    REQUIRE(writer.HandleWrite("event-2", "metadata-2"));

    // Then they're split into two files just the same
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(
        storage.Cat(relpaths[0]) == MockTLVFile()
                                        .AppendMetadata("metadata-0")
                                        .AppendEvent("event-0")
                                        .AppendMetadata("metadata-1")
                                        .AppendEvent("event-1")
                                        .ToString()
    );
    REQUIRE(relpaths[1] == "pending/1700000000020");
    REQUIRE(
        storage.Cat(relpaths[1]) ==
        MockTLVFile().AppendMetadata("metadata-2").AppendEvent("event-2").ToString()
    );
  }

  SECTION("M drop event W max writes per file is zero") {
    // Given a batch writer configured to write no more than 0 events per file
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = 0;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we attempt to write a payload of any size, at any time
    const bool write_ok = writer.HandleWrite("event-0", {});

    // Then the event is not accepted
    REQUIRE(write_ok == false);

    // And nothing is written to disk
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.empty());
  }

  SECTION("M drop event W max writes per file is negative") {
    // Given a batch writer configured to write no more than -1 events per file
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Large);
    config.max_writes_per_file = -1;
    BatchWriter writer = init_writer(clock, storage, config);

    // When we attempt to write a payload of any size, at any time
    const bool write_ok = writer.HandleWrite("event-0", {});

    // Then the event is not accepted
    REQUIRE(write_ok == false);

    // And nothing is written to disk
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.empty());
  }

  SECTION("M find not-in-use filename W chosen name conflicts with existing file") {
    // Given an ordinary BatchWriter at start time 1700000000000
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(clock, storage, config);

    // And an event directory that already contains files named '1700000000000',
    // '1700000000001', and '1700000000002'
    storage.WithExistingFile("pending/1700000000000", "non");
    storage.WithExistingFile("pending/1700000000001", "net");
    storage.WithExistingFile("pending/1700000000002", "nie");
    storage.WithExistingFile("pending/1700000000004", "nope");

    // When we write an event, Then it succeeds
    REQUIRE(writer.HandleWrite("event-0", {}));

    // And the newly-written event is stored in a file whose name reflects the next
    // available timestamp value
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 5);
    REQUIRE(relpaths[3] == "pending/1700000000003");
    REQUIRE(
        storage.Cat(relpaths[3]) == MockTLVFile().AppendEvent("event-0").ToString()
    );
  }

  SECTION("M drop event W all potential filenames are already in use") {
    // Given an ordinary BatchWriter at start time 1700000000000
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
    BatchWriter writer = init_writer(clock, storage, config);

    // And an event directory that already contains a file named with every possible
    // millisecond-precision timestamp value between now and 100ms from now
    for (uint64_t i = 0; i < 100; i++) {
      const uint64_t timestamp_ms = 1700000000000 + i;
      const std::string relpath = "pending/" + std::to_string(timestamp_ms);
      storage.WithExistingFile(relpath, "no dice");
    }

    // When we attempt to write an event
    bool write_ok = writer.HandleWrite("event-0", {});

    // Then the event is rejected
    REQUIRE(write_ok == false);

    // And no files are modified
    std::vector<std::string> relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 100);
    const std::string expected = MockTLVFile().AppendBytes("no dice").ToString();
    for (const std::string& relpath : relpaths) {
      REQUIRE(storage.Cat(relpath) == expected);
    }

    // But: When some time passes and we attempt another write
    clock.TickMilliseconds(100);
    write_ok = writer.HandleWrite("event-1", {});

    // Then the event is accepted
    REQUIRE(write_ok == true);

    // And a new batch file is created
    relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 101);
    REQUIRE(
        std::find(relpaths.begin(), relpaths.end(), "pending/1700000000100") !=
        relpaths.end()
    );
  }

  SECTION("M remove all files in pending W tracking consent changes to NotGranted") {
    // Given a BatchWriter configured to write to 'pending/'
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1"));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then we have two batch files in our directory, as expected
    auto relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(relpaths[1] == "pending/1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent has been revoked
    const bool ok = writer.SetTrackingConsent(TrackingConsent::NotGranted);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory is now empty
    relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 0);

    // And our granted directory is empty as ever
    relpaths = storage.FindFiles("granted");
    REQUIRE(relpaths.size() == 0);

    // And if we subsequently write an event, it is dropped rather than being flushed to
    // storage, since tracking consent is explicitly revoked
    REQUIRE(writer.HandleWrite("event-3", {}));
    REQUIRE(storage.FindFiles("pending").size() == 0);
    REQUIRE(storage.FindFiles("granted").size() == 0);
  }

  SECTION(
      "M move all files from pending to granted W tracking consent changes to Granted"
  ) {
    // Given a BatchWriter configured to write to 'pending/'
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1"));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then we have two batch files in our directory, as expected
    auto relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(relpaths[1] == "pending/1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is now granted
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Granted);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory is now empty
    relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 0);

    // And our granted directory now contains the two batches that were previously
    // written to pending
    relpaths = storage.FindFiles("granted");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "granted/1700000000000");
    REQUIRE(relpaths[1] == "granted/1700000060100");

    // And if we subsequently write an event, it's flushed to storage directly to the
    // 'granted' directory
    REQUIRE(writer.HandleWrite("event-3", {}));
    REQUIRE(storage.FindFiles("pending").size() == 0);
    relpaths = storage.FindFiles("granted");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 3);
    REQUIRE(relpaths[0] == "granted/1700000000000");
    REQUIRE(relpaths[1] == "granted/1700000060100");

    // And we've create a brand new batch file to contain our new data; we don't
    // continue writing to the old file post-move
    REQUIRE(relpaths[2] == "granted/1700000060101");
  }

  SECTION(
      "M handle conflict by deleting file from pending W both pending and granted "
      "contain a file by that name"
  ) {
    // Given a 'granted/' directory that already contains a file named '1700000000000'
    MockClock clock;
    MockStorageDirectory storage;
    storage.WithExistingFile("granted/1700000000000", "existing-contents");

    // And a BatchWriter configured to write to 'pending/'
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1"));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then we have two batch files in our directory, as expected
    auto relpaths = storage.FindFiles("pending");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "pending/1700000000000");
    REQUIRE(relpaths[1] == "pending/1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is now granted
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Granted);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory is now empty
    relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 0);

    // And our granted directory now contains the two batches that were previously
    // written to pending
    relpaths = storage.FindFiles("granted");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "granted/1700000000000");
    REQUIRE(relpaths[1] == "granted/1700000060100");

    // But granted/1700000000000 contains the original, unmodified contents that were
    // present initially, while granted/1700000060100 has been newly moved
    REQUIRE(storage.Cat("granted/1700000000000") == "existing-contents");
    REQUIRE(
        storage.Cat("granted/1700000060100") ==
        MockTLVFile().AppendEvent("event-2").ToString()
    );
  }

  SECTION("M fail W unable to move files from pending dir to granted dir") {
    // Given a BatchWriter and an event in 'pending/'
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config);
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(100);

    // And a 'granted/' directory that can not be written to
    storage.WithExistingFile("granted/.dummy", "ensures-that-granted-dir-exists");
    storage.SetFail("granted", true);

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is now granted
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Granted);

    // Then the operation fails
    REQUIRE(!ok);

    // And our pending directory retains its original contents, unmodified
    REQUIRE(storage.FindFiles("pending").size() == 1);
    REQUIRE(
        storage.Cat("pending/1700000000000") ==
        MockTLVFile().AppendEvent("event-0").ToString()
    );

    // And our granted directory contains no event batches
    auto relpaths = storage.FindFiles("granted");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "granted/.dummy");
  }

  SECTION(
      "M move no files but begin writing to pending dir W tracking consent changes to "
      "Pending"
  ) {
    // Given a batch writer configured to write to granted/
    MockClock clock;
    MockStorageDirectory storage;
    auto config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
    BatchWriter writer = init_writer(clock, storage, config, TrackingConsent::Granted);

    // And a succession of event writes that should result in two batch files being
    // written
    REQUIRE(writer.HandleWrite("event-0", {}));
    clock.TickMilliseconds(100);
    REQUIRE(writer.HandleWrite("event-1", "metadata-1"));
    clock.Tick(std::chrono::seconds(60));
    REQUIRE(writer.HandleWrite("event-2", {}));

    // Then we have two batch files in our directory, as expected
    auto relpaths = storage.FindFiles("granted");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "granted/1700000000000");
    REQUIRE(relpaths[1] == "granted/1700000060100");

    // Next: When we call SetTrackingConsent() to notify the BatchWriter that the user's
    // tracking consent is once again pending
    const bool ok = writer.SetTrackingConsent(TrackingConsent::Pending);

    // Then the operation succeeds
    REQUIRE(ok);

    // And our pending directory remains empty
    relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 0);

    // And our granted directory still contains the two batches that were placed there
    // while consent was granted
    relpaths = storage.FindFiles("granted");
    std::sort(relpaths.begin(), relpaths.end());
    REQUIRE(relpaths.size() == 2);
    REQUIRE(relpaths[0] == "granted/1700000000000");
    REQUIRE(relpaths[1] == "granted/1700000060100");

    // And if we subsequently write an event, it's flushed to the 'pending' directory
    clock.TickMilliseconds(5);
    REQUIRE(writer.HandleWrite("event-3", {}));
    REQUIRE(storage.FindFiles("granted").size() == 2);
    relpaths = storage.FindFiles("pending");
    REQUIRE(relpaths.size() == 1);
    REQUIRE(relpaths[0] == "pending/1700000060105");
  }
}
