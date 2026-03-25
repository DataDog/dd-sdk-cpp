// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/storage/event.hpp"

#include <string>
#include <vector>

#include "mock/clock.hpp"
#include "mock/filesystem_new.hpp"
#include "mock/tlv.hpp"
#include "support/catch.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;

namespace {

bool DirectoryExists(MockFilesystemNew& fs, std::string_view path) {
  PlatformPath pp;
  std::string s(path);
  if (!pp.Encode(s.c_str())) {
    return false;
  }
  std::vector<std::string> names;
  return fs.ListSubdirectories(pp, names) == FilesystemResult::OK;
}

}  // namespace

TEST_CASE("EventStorage", "[unit][storage]") {
  MockFilesystemNew fs;
  MockClock clock;
  clock.FreezeAtMilliseconds(1700000000000LL);
  DiagnosticMessageBuffer diagnostics;
  fs.Mkdirs("storage/main");

  SECTION("M create feature and consent subdirectories W Initialize is called") {
    // Given an EventStorage for the "rum" feature
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );

    // When Initialize is called with the storage root
    REQUIRE(storage.Initialize("storage/main"));

    // Then both the granted (v1) and pending (intermediate-v1) subdirectories exist
    REQUIRE(DirectoryExists(fs, "storage/main/rum/v1"));
    REQUIRE(DirectoryExists(fs, "storage/main/rum/intermediate-v1"));
  }

  SECTION("M write to v1 W consent is Granted") {
    // Given an EventStorage with consent Granted
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When an event is written
    REQUIRE(storage.Write("event-data", ""));

    // Then a file appears in v1 named after the current timestamp in milliseconds
    auto files = fs.FindFiles("storage/main/rum/v1");
    REQUIRE(files.size() == 1);
    REQUIRE(files[0] == "1700000000000");

    // And the file contains the event encoded in TLV format
    REQUIRE(
        fs.Cat("storage/main/rum/v1/1700000000000") ==
        MockTLVFile().AppendEvent("event-data").ToString()
    );
  }

  SECTION("M write to intermediate-v1 W consent is Pending") {
    // Given an EventStorage with consent Pending
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Pending,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When an event is written
    REQUIRE(storage.Write("event-data", ""));

    // Then the event is routed to intermediate-v1, not v1
    auto files = fs.FindFiles("storage/main/rum/intermediate-v1");
    REQUIRE(files.size() == 1);
    REQUIRE(files[0] == "1700000000000");
    REQUIRE(
        fs.Cat("storage/main/rum/intermediate-v1/1700000000000") ==
        MockTLVFile().AppendEvent("event-data").ToString()
    );
    REQUIRE(fs.FindFiles("storage/main/rum/v1").empty());
  }

  SECTION("M succeed as no-op W consent is NotGranted") {
    // Given an EventStorage with consent NotGranted
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::NotGranted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When an event is written
    REQUIRE(storage.Write("event-data", ""));

    // Then no files are created in either directory
    REQUIRE(fs.FindFiles("storage/main/rum/v1").empty());
    REQUIRE(fs.FindFiles("storage/main/rum/intermediate-v1").empty());
  }

  SECTION("M prepend metadata block before event block W metadata is non-empty") {
    // Given an EventStorage with consent Granted
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When an event is written with associated metadata
    REQUIRE(storage.Write("event-data", "meta-data"));

    // Then the file contains a TLV metadata block followed by the TLV event block
    REQUIRE(
        fs.Cat("storage/main/rum/v1/1700000000000") ==
        MockTLVFile().AppendMetadata("meta-data").AppendEvent("event-data").ToString()
    );
  }

  SECTION("M drop event and return false W single event exceeds max_file_size") {
    // Given an EventStorage configured with a max file size of 1 byte
    EventStorageConfig config{std::chrono::hours{1}};
    config.max_file_size = 1;
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When an event that exceeds the max file size is written
    REQUIRE(!storage.Write("event-data", ""));

    // Then an error is logged and no file is created
    REQUIRE(!diagnostics.error.empty());
    REQUIRE(fs.FindFiles("storage/main/rum/v1").empty());
  }

  SECTION("M start new file W max_writes_per_file is reached") {
    // Given an EventStorage configured to hold at most 2 events per file
    EventStorageConfig config{std::chrono::hours{1}};
    config.max_writes_per_file = 2;
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When 3 events are written, ticking the clock between writes to ensure
    // unique filenames when a new file is opened
    REQUIRE(storage.Write("e1", ""));
    clock.TickMilliseconds(1);
    REQUIRE(storage.Write("e2", ""));
    clock.TickMilliseconds(1);
    REQUIRE(storage.Write("e3", ""));

    // Then the first two events share a file and the third is in a new file
    REQUIRE(fs.FindFiles("storage/main/rum/v1").size() == 2);
  }

  SECTION("M start new file W max_file_size would be exceeded") {
    // Each write of a 10-byte event costs TLVBlockHeader::SIZE (6) + 10 = 16 bytes.
    // With max_file_size=16, the second write would bring the file to 32 bytes, so a
    // new file is opened instead.
    //
    // Given an EventStorage configured with a max file size of exactly one event's
    // worth
    EventStorageConfig config{std::chrono::hours{1}};
    config.max_file_size = 16;
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When two events are written that together would exceed the file size limit
    REQUIRE(storage.Write("1234567890", ""));
    clock.TickMilliseconds(1);
    REQUIRE(storage.Write("1234567890", ""));

    // Then each event is written to a separate file
    REQUIRE(fs.FindFiles("storage/main/rum/v1").size() == 2);
  }

  SECTION("M start new file W file age has exceeded max_file_age") {
    // Given an EventStorage configured with a 100ms max file age
    EventStorageConfig config{std::chrono::milliseconds{100}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When the first event is written and the clock advances past the age limit
    REQUIRE(storage.Write("e1", ""));
    clock.TickMilliseconds(200);

    // And a second event is written
    REQUIRE(storage.Write("e2", ""));

    // Then the second event is written to a new file
    REQUIRE(fs.FindFiles("storage/main/rum/v1").size() == 2);
  }

  SECTION("M use next available ms W filename at current timestamp already exists") {
    // Given an EventStorage and a pre-existing file at the current clock timestamp
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    fs.Touch("storage/main/rum/v1/1700000000000");

    // When an event is written
    REQUIRE(storage.Write("event-data", ""));

    // Then the event is written to a new file at the next available millisecond
    REQUIRE(fs.FindFiles("storage/main/rum/v1").size() == 2);
    REQUIRE(
        fs.Cat("storage/main/rum/v1/1700000000001") ==
        MockTLVFile().AppendEvent("event-data").ToString()
    );
  }

  SECTION("M return false W all 100 filename slots in use") {
    // Given an EventStorage and all 100 consecutive filename slots already occupied
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    for (int i = 0; i < 100; i++) {
      fs.Touch("storage/main/rum/v1/" + std::to_string(1700000000000LL + i));
    }

    // When an event is written with no available filename slots
    REQUIRE(!storage.Write("event-data", ""));
  }

  SECTION("M close open batch file handle W Flush is called") {
    // Given an EventStorage with an open batch file handle (created by a Write)
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    REQUIRE(fs.GetOpenHandles().empty());

    REQUIRE(storage.Write("event-data", ""));
    REQUIRE(!fs.GetOpenHandles().empty());

    // When Flush is called
    storage.Flush();

    // Then the batch file handle is closed
    REQUIRE(fs.GetOpenHandles().empty());
  }

  SECTION("M close open batch file handle W EventStorage is destroyed") {
    // Given an EventStorage with an open batch file handle (created by a Write)
    EventStorageConfig config{std::chrono::hours{1}};
    {
      EventStorage storage(
          fs,
          "rum",
          diagnostics.CreateTestLogger(),
          TrackingConsent::Granted,
          clock,
          config
      );
      REQUIRE(storage.Initialize("storage/main"));
      REQUIRE(storage.Write("event-data", ""));
      REQUIRE(!fs.GetOpenHandles().empty());
    }

    // Then the batch file handle is closed on destruction
    REQUIRE(fs.GetOpenHandles().empty());
  }

  SECTION(
      "M move files from intermediate-v1 to v1 W consent changes from Pending to "
      "Granted"
  ) {
    // Given events written while consent is Pending
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Pending,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    REQUIRE(storage.Write("event-data", ""));
    storage.Flush();

    REQUIRE(!fs.FindFiles("storage/main/rum/intermediate-v1").empty());
    REQUIRE(fs.FindFiles("storage/main/rum/v1").empty());

    // When consent changes to Granted
    REQUIRE(storage.SetConsent(TrackingConsent::Granted));

    // Then the pending files are moved to v1 for the upload thread to pick up
    REQUIRE(fs.FindFiles("storage/main/rum/intermediate-v1").empty());
    REQUIRE(!fs.FindFiles("storage/main/rum/v1").empty());
    REQUIRE(
        fs.Cat("storage/main/rum/v1/1700000000000") ==
        MockTLVFile().AppendEvent("event-data").ToString()
    );
  }

  SECTION(
      "M delete files from intermediate-v1 W consent changes from Pending to NotGranted"
  ) {
    // Given events written while consent is Pending
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Pending,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    REQUIRE(storage.Write("event-data", ""));
    storage.Flush();

    const int deleted_before = fs.GetNumFilesDeleted();

    // When consent changes to NotGranted
    REQUIRE(storage.SetConsent(TrackingConsent::NotGranted));

    // Then the pending files are deleted
    REQUIRE(fs.FindFiles("storage/main/rum/intermediate-v1").empty());
    REQUIRE(fs.GetNumFilesDeleted() > deleted_before);
  }

  SECTION("M leave v1 files alone W consent changes from Granted to NotGranted") {
    // Given events written while consent is Granted
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    REQUIRE(storage.Write("event-data", ""));
    storage.Flush();

    const auto v1_files_before = fs.FindFiles("storage/main/rum/v1");
    const int deleted_before = fs.GetNumFilesDeleted();

    // When consent changes to NotGranted
    REQUIRE(storage.SetConsent(TrackingConsent::NotGranted));

    // Then v1 files are left untouched for the upload thread to drain
    REQUIRE(fs.FindFiles("storage/main/rum/v1") == v1_files_before);
    REQUIRE(fs.GetNumFilesDeleted() == deleted_before);
  }

  SECTION(
      "M write to intermediate-v1 after reverting W consent changes from Granted to "
      "Pending"
  ) {
    // Given an EventStorage with consent Granted
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));

    // When consent reverts to Pending
    REQUIRE(storage.SetConsent(TrackingConsent::Pending));

    // And an event is written
    REQUIRE(storage.Write("event-data", ""));

    // Then the event is routed to intermediate-v1, not v1
    REQUIRE(!fs.FindFiles("storage/main/rum/intermediate-v1").empty());
    REQUIRE(fs.FindFiles("storage/main/rum/v1").empty());
  }

  SECTION("M succeed as no-op W SetConsent called with current consent value") {
    // Given an EventStorage with consent Granted and some events already written
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Granted,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    REQUIRE(storage.Write("e1", ""));
    storage.Flush();

    const auto v1_files_before = fs.FindFiles("storage/main/rum/v1");

    // When SetConsent is called with the already-current value
    REQUIRE(storage.SetConsent(TrackingConsent::Granted));

    // Then no files are moved or deleted
    REQUIRE(fs.FindFiles("storage/main/rum/v1") == v1_files_before);
    REQUIRE(diagnostics.error.empty());
  }

  SECTION(
      "M delete source and continue W filename conflict during Pending to Granted "
      "migration"
  ) {
    // Given events written while consent is Pending, and a file already present in
    // v1 with the same timestamp-derived name
    EventStorageConfig config{std::chrono::hours{1}};
    EventStorage storage(
        fs,
        "rum",
        diagnostics.CreateTestLogger(),
        TrackingConsent::Pending,
        clock,
        config
    );
    REQUIRE(storage.Initialize("storage/main"));
    REQUIRE(storage.Write("pending-data", ""));
    storage.Flush();

    // Pre-create a file in v1 with the same name to force a rename conflict
    fs.Touch("storage/main/rum/v1/1700000000000", "existing-data");

    // When consent changes to Granted
    REQUIRE(storage.SetConsent(TrackingConsent::Granted));

    // Then the destination file (pre-existing) is preserved and the conflicting
    // source file is deleted
    auto v1_files = fs.FindFiles("storage/main/rum/v1");
    REQUIRE(v1_files.size() == 1);
    REQUIRE(fs.Cat("storage/main/rum/v1/1700000000000") == "existing-data");
    REQUIRE(fs.FindFiles("storage/main/rum/intermediate-v1").empty());
  }
}
