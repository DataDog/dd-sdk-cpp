#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "core/core.hpp"
#include "core/storage.hpp"
#include "mock/clock.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("StorageMessage", "[unit]")
{
    SECTION("M create TrackingConsentChanged message W static factory method used")
    {
        // Given a tracking consent value
        TrackingConsent consent = TrackingConsent::Granted;

        // When creating a TrackingConsentChanged message
        auto message = StorageMessage::TrackingConsentChanged(consent);

        // Then the message has the correct type and payload
        REQUIRE(message.type == StorageMessageType::TrackingConsentChanged);
        REQUIRE(
            message.payload.tracking_consent_changed.value == TrackingConsent::Granted
        );
    }

    SECTION("M create EventGenerated message W static factory method used")
    {
        // Given feature data and event blocks
        FeatureId feature_id = CreateFeatureId("TEST");
        Block event_data{"test event data"};
        Block metadata_data{"test metadata"};

        // When creating an EventGenerated message
        auto message =
            StorageMessage::EventGenerated(feature_id, event_data, metadata_data);

        // Then the message has the correct type and payload
        REQUIRE(message.type == StorageMessageType::EventGenerated);
        REQUIRE(message.payload.event_generated.feature_id == feature_id);

        // And the event data is copied correctly
        std::vector<uint8_t> expected_event(event_data.begin(), event_data.end());
        REQUIRE(message.payload.event_generated.event == expected_event);

        // And the metadata is copied correctly
        std::vector<uint8_t> expected_metadata(
            metadata_data.begin(), metadata_data.end()
        );
        REQUIRE(message.payload.event_generated.event_metadata == expected_metadata);
    }

    SECTION("M handle empty metadata W EventGenerated message created")
    {
        // Given feature data with empty metadata
        FeatureId feature_id = CreateFeatureId("LOGS");
        Block event_data{"event without metadata"};
        Block empty_metadata{};

        // When creating an EventGenerated message with empty metadata
        auto message =
            StorageMessage::EventGenerated(feature_id, event_data, empty_metadata);

        // Then the message is created successfully with empty metadata
        REQUIRE(message.type == StorageMessageType::EventGenerated);
        REQUIRE(message.payload.event_generated.event_metadata.empty());
        REQUIRE(!message.payload.event_generated.event.empty());
    }

    SECTION("M move construct properly W union contains TrackingConsentChanged")
    {
        // Given a TrackingConsentChanged message
        auto original =
            StorageMessage::TrackingConsentChanged(TrackingConsent::Pending);

        // When move constructing from it
        auto moved = std::move(original);

        // Then the moved message has the correct data
        REQUIRE(moved.type == StorageMessageType::TrackingConsentChanged);
        REQUIRE(
            moved.payload.tracking_consent_changed.value == TrackingConsent::Pending
        );
    }

    SECTION("M move construct properly W union contains EventGenerated")
    {
        // Given an EventGenerated message with significant data
        FeatureId feature_id = CreateFeatureId("MOVE");
        Block event_data{"data to be moved"};
        Block metadata_data{"metadata to move"};
        auto original =
            StorageMessage::EventGenerated(feature_id, event_data, metadata_data);

        // When move constructing from it
        auto moved = std::move(original);

        // Then the moved message has the correct data
        REQUIRE(moved.type == StorageMessageType::EventGenerated);
        REQUIRE(moved.payload.event_generated.feature_id == feature_id);

        std::vector<uint8_t> expected_event(event_data.begin(), event_data.end());
        REQUIRE(moved.payload.event_generated.event == expected_event);

        std::vector<uint8_t> expected_metadata(
            metadata_data.begin(), metadata_data.end()
        );
        REQUIRE(moved.payload.event_generated.event_metadata == expected_metadata);
    }

    SECTION("M move assign properly W source and destination have same union type")
    {
        // Given two TrackingConsentChanged messages
        auto source = StorageMessage::TrackingConsentChanged(TrackingConsent::Granted);
        auto destination =
            StorageMessage::TrackingConsentChanged(TrackingConsent::NotGranted);

        // When move assigning
        destination = std::move(source);

        // Then the destination has the source's data
        REQUIRE(destination.type == StorageMessageType::TrackingConsentChanged);
        REQUIRE(
            destination.payload.tracking_consent_changed.value ==
            TrackingConsent::Granted
        );
    }

    SECTION(
        "M move assign properly W source and destination have different union types"
    )
    {
        // Given a TrackingConsentChanged message and an EventGenerated message
        auto consent_msg =
            StorageMessage::TrackingConsentChanged(TrackingConsent::Pending);
        FeatureId feature_id = CreateFeatureId("SWAP");
        Block event_data{"swapped event"};
        Block metadata_data{"swapped meta"};
        auto event_msg =
            StorageMessage::EventGenerated(feature_id, event_data, metadata_data);

        // When move assigning from different union types
        consent_msg = std::move(event_msg);

        // Then the destination correctly adopts the source's type and data
        REQUIRE(consent_msg.type == StorageMessageType::EventGenerated);
        REQUIRE(consent_msg.payload.event_generated.feature_id == feature_id);

        std::vector<uint8_t> expected_event(event_data.begin(), event_data.end());
        REQUIRE(consent_msg.payload.event_generated.event == expected_event);

        std::vector<uint8_t> expected_metadata(
            metadata_data.begin(), metadata_data.end()
        );
        REQUIRE(
            consent_msg.payload.event_generated.event_metadata == expected_metadata
        );
    }

    SECTION("M properly destruct W union contains EventGenerated with vectors")
    {
        // Given an EventGenerated message in a limited scope
        {
            FeatureId feature_id = CreateFeatureId("DEST");
            Block large_event_data{
                "this is a large event data block that will test destruction"
            };
            Block large_metadata{"large metadata block for destruction testing"};

            // When creating and then destroying the message
            auto message = StorageMessage::EventGenerated(
                feature_id, large_event_data, large_metadata
            );

            // Then destruction should happen without issues when message goes out of
            // scope
            REQUIRE(message.type == StorageMessageType::EventGenerated);
        }
        // Destructor is called here - test passes if no crashes occur
    }
}

TEST_CASE("BatchWriter", "[unit]")
{
    auto init_writer = [](MockClock& clock,
                          MockStorageDirectory& storage,
                          const BatchWriterConfig config)
    {
        // Set the system clock to a known timestamp
        clock.FreezeAtMilliseconds(1700000000000);

        // Prepare a mock directory to contain events
        auto directory = storage.PrepareSubdirectory("events");
        REQUIRE(directory.has_value());

        // Return an initialized writer
        return BatchWriter(std::move(*directory), clock, config);
    };

    SECTION("M successfully write event W valid event data provided")
    {
        // Given a BatchWriter configured to write to 'events/'
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.size() == 1);
        std::string expected = MockTLVFile()
                                   .AppendEvent("event-0")
                                   .AppendMetadata("metadata-1")
                                   .AppendEvent("event-1")
                                   .AppendEvent("event-2")
                                   .ToString();
        REQUIRE(storage.Cat(relpaths.front()) == expected);

        // And the name of that file matches the system time when it was created
        REQUIRE(relpaths.front() == "events/1700000000000");
    }

    SECTION("M handle successive writes in the same file W limits are not met")
    {
        // Given a batch writer with reasonable default values, and a start time of
        // 1700000000000
        MockClock clock;
        MockStorageDirectory storage;
        auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
        BatchWriter writer = init_writer(clock, storage, config);

        // When we write a succession of 50 small events, with 16ms elapsing between
        // each event (for a total duration of 0.8 seconds)
        MockTLVFile want(1024);
        for (int i = 0; i < 50; i++)
        {
            std::string event_metadata = "m-" + std::to_string(i);
            std::string event = "e-" + std::to_string(i);
            want.AppendMetadata(event_metadata);
            want.AppendEvent(event);
            writer.HandleWrite(event, event_metadata);
            clock.TickMilliseconds(16);
        }

        // Then all of those events are contained in a single batch file
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.size() == 1);
        REQUIRE(relpaths.front() == "events/1700000000000");
        REQUIRE(storage.Cat(relpaths.front()) == want.ToString());
    }

    SECTION("M start new file W max file age exceeded {BatchSize::Small ~ 3s}")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);

        // And the first file contains the first two events and has a name matching our
        // start timestamp
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) ==
            MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
        );

        // And the second file contains the last event and has a name matches the time
        // four seconds later
        REQUIRE(relpaths[1] == "events/1700000004000");
        REQUIRE(
            storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
        );
    }

    SECTION("M start new file W max file age exceeded {BatchSize::Medium ~ 10s}")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);

        // And the first file matches our start time and has the first two events
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) ==
            MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
        );

        // And the second file matches our end time and has the last event
        REQUIRE(relpaths[1] == "events/1700000010000");
        REQUIRE(
            storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
        );
    }

    SECTION("M start new file W max file age exceeded {BatchSize::Large ~ 35s}")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);

        // And the first file matches our start time and has the first two events
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) ==
            MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
        );

        // And the second file matches our end time and has the last event
        REQUIRE(relpaths[1] == "events/1700000035000");
        REQUIRE(
            storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
        );
    }

    SECTION("M start new file W max file size exceeded")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);

        // And the first file matches our start time and has the first three events
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) == MockTLVFile()
                                            .AppendEvent("twenty-bytes-0")
                                            .AppendEvent("twenty-bytes-1")
                                            .AppendEvent("twenty-bytes-2")
                                            .ToString()
        );

        // And the second file matches our end time and has the last event
        REQUIRE(relpaths[1] == "events/1700000000030");
        REQUIRE(
            storage.Cat(relpaths[1]) ==
            MockTLVFile().AppendEvent("twenty-bytes-3").ToString()
        );
    }

    SECTION("M consider both event and metadata W computing size impact of write")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);

        // And the first file matches our start time and has the event and metadata
        // blocks for our first event
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) == MockTLVFile()
                                            .AppendMetadata("twenty-bytes-a")
                                            .AppendEvent("twenty-bytes-0")
                                            .ToString()
        );

        // And the second file matches our end time and has the event and metadata
        // blocks for the second event
        REQUIRE(relpaths[1] == "events/1700000000010");
        REQUIRE(
            storage.Cat(relpaths[1]) == MockTLVFile()
                                            .AppendMetadata("twenty-bytes-b")
                                            .AppendEvent("twenty-bytes-1")
                                            .ToString()
        );
    }

    SECTION("M write to same file W file size would exactly match max file size")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.size() == 1);
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) == MockTLVFile()
                                            .AppendEvent("twenty-bytes-0")
                                            .AppendEvent("twenty-bytes-1")
                                            .ToString()
        );
    }

    SECTION("M start new file W new file size would exceed max file size by 1 byte")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) ==
            MockTLVFile().AppendEvent("twenty-bytes-0").ToString()
        );
        REQUIRE(relpaths[1] == "events/1700000000010");
        REQUIRE(
            storage.Cat(relpaths[1]) ==
            MockTLVFile().AppendEvent("twenty-bytes-1").ToString()
        );
    }

    SECTION("M drop event W single write exceeds max file size")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.empty());
    }

    SECTION("M start new file W max writes per file exceeded")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) ==
            MockTLVFile().AppendEvent("event-0").AppendEvent("event-1").ToString()
        );
        REQUIRE(relpaths[1] == "events/1700000000020");
        REQUIRE(
            storage.Cat(relpaths[1]) == MockTLVFile().AppendEvent("event-2").ToString()
        );
    }

    SECTION("M consider both event and metadata W evaluating max writes per file")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 2);
        REQUIRE(relpaths[0] == "events/1700000000000");
        REQUIRE(
            storage.Cat(relpaths[0]) == MockTLVFile()
                                            .AppendMetadata("metadata-0")
                                            .AppendEvent("event-0")
                                            .AppendMetadata("metadata-1")
                                            .AppendEvent("event-1")
                                            .ToString()
        );
        REQUIRE(relpaths[1] == "events/1700000000020");
        REQUIRE(
            storage.Cat(relpaths[1]) ==
            MockTLVFile().AppendMetadata("metadata-2").AppendEvent("event-2").ToString()
        );
    }

    SECTION("M drop event W max writes per file is zero")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.empty());
    }

    SECTION("M drop event W max writes per file is negative")
    {
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
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.empty());
    }

    SECTION("M find not-in-use filename W chosen name conflicts with existing file")
    {
        // Given an ordinary BatchWriter at start time 1700000000000
        MockClock clock;
        MockStorageDirectory storage;
        auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
        BatchWriter writer = init_writer(clock, storage, config);

        // And an event directory that already contains files named '1700000000000',
        // '1700000000001', and '1700000000002'
        storage.WithExistingFile("events/1700000000000", "non");
        storage.WithExistingFile("events/1700000000001", "net");
        storage.WithExistingFile("events/1700000000002", "nie");
        storage.WithExistingFile("events/1700000000004", "nope");

        // When we write an event, Then it succeeds
        REQUIRE(writer.HandleWrite("event-0", {}));

        // And the newly-written event is stored in a file whose name reflects the next
        // available timestamp value
        std::vector<std::string> relpaths = storage.FindFiles("events");
        std::sort(relpaths.begin(), relpaths.end());
        REQUIRE(relpaths.size() == 5);
        REQUIRE(relpaths[3] == "events/1700000000003");
        REQUIRE(
            storage.Cat(relpaths[3]) == MockTLVFile().AppendEvent("event-0").ToString()
        );
    }

    SECTION("M drop event W all potential filenames are already in use")
    {
        // Given an ordinary BatchWriter at start time 1700000000000
        MockClock clock;
        MockStorageDirectory storage;
        auto config = BatchWriterConfig::FromBatchSize(BatchSize::Medium);
        BatchWriter writer = init_writer(clock, storage, config);

        // And an event directory that already contains a file named with every possible
        // millisecond-precision timestamp value between now and 100ms from now
        for (uint64_t i = 0; i < 100; i++)
        {
            const uint64_t timestamp_ms = 1700000000000 + i;
            const std::string relpath = "events/" + std::to_string(timestamp_ms);
            storage.WithExistingFile(relpath, "no dice");
        }

        // When we attempt to write an event
        bool write_ok = writer.HandleWrite("event-0", {});

        // Then the event is rejected
        REQUIRE(write_ok == false);

        // And no files are modified
        std::vector<std::string> relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.size() == 100);
        const std::string expected = MockTLVFile().AppendBytes("no dice").ToString();
        for (const std::string& relpath : relpaths)
        {
            REQUIRE(storage.Cat(relpath) == expected);
        }

        // But: When some time passes and we attempt another write
        clock.TickMilliseconds(100);
        write_ok = writer.HandleWrite("event-1", {});

        // Then the event is accepted
        REQUIRE(write_ok == true);

        // And a new batch file is created
        relpaths = storage.FindFiles("events");
        REQUIRE(relpaths.size() == 101);
        REQUIRE(
            std::find(relpaths.begin(), relpaths.end(), "events/1700000000100") !=
            relpaths.end()
        );
    }

    // TODO: Test Delete/MigrateTo once implemented
}

TEST_CASE("EventStorage", "[unit]")
{
    // Common test setup logic
    auto prepare_storage = [](MockStorageDirectory& mock_storage,
                              MockClock& clock,
                              TrackingConsent consent) -> EventStorage
    {
        // 'events/no-upload' contains pending events; 'events/yes-upload' for granted
        auto directory = mock_storage.PrepareSubdirectory("events");
        REQUIRE(directory.has_value());
        auto pending_subdir = (*directory)->PrepareSubdirectory("no-upload");
        REQUIRE(pending_subdir.has_value());
        auto granted_subdir = (*directory)->PrepareSubdirectory("yes-upload");
        REQUIRE(granted_subdir.has_value());

        // Make two BatchWriters, one to handle writes for each directory
        auto writer_config = BatchWriterConfig::FromBatchSize(BatchSize::Small);
        auto pending_writer = std::make_unique<BatchWriter>(
            std::move(*pending_subdir), clock, writer_config
        );
        auto granted_writer = std::make_unique<BatchWriter>(
            std::move(*granted_subdir), clock, writer_config
        );

        // Create an EventStorage interface to sit in front of them, using the desired
        // value for our initial tracking consent
        return EventStorage(
            consent, std::move(pending_writer), std::move(granted_writer)
        );
    };

    SECTION("M write events to pending subdir W initial tracking consent is pending")
    {
        // Given an EventStorage whose tracking consent is initially pending
        MockStorageDirectory mock_storage;
        MockClock clock;
        EventStorage storage =
            prepare_storage(mock_storage, clock, TrackingConsent::Pending);

        // When we write an event
        const bool ok = storage.HandleWrite("while-pending", {});

        // Then the write should succeed
        REQUIRE(ok);

        // And the file containing that event should be created in our pending subdir
        auto pending_relpaths = mock_storage.FindFiles("events/no-upload");
        REQUIRE(pending_relpaths.size() == 1);
        REQUIRE(
            mock_storage.Cat(pending_relpaths.front()) ==
            MockTLVFile().AppendEvent("while-pending").ToString()
        );

        // And our granted subdir should remain empty
        auto granted_relpaths = mock_storage.FindFiles("events/yes-upload");
        REQUIRE(granted_relpaths.size() == 0);
    }

    SECTION("M write events to granted subdir W initial tracking consent is granted")
    {
        // Given an EventStorage whose tracking consent is initially granted
        MockStorageDirectory mock_storage;
        MockClock clock;
        EventStorage storage =
            prepare_storage(mock_storage, clock, TrackingConsent::Granted);

        // When we write an event
        const bool ok = storage.HandleWrite("while-granted", {});

        // Then the write should succeed
        REQUIRE(ok);

        // And our pending subdir should remain empty
        auto pending_relpaths = mock_storage.FindFiles("events/no-upload");
        REQUIRE(pending_relpaths.size() == 0);

        // And our granted subdir should contain the file with our event
        auto granted_relpaths = mock_storage.FindFiles("events/yes-upload");
        REQUIRE(granted_relpaths.size() == 1);
        REQUIRE(
            mock_storage.Cat(granted_relpaths.front()) ==
            MockTLVFile().AppendEvent("while-granted").ToString()
        );
    }

    SECTION("M write events nowhere W initial tracking consent is not granted")
    {
        // Given an EventStorage whose tracking consent is initially granted
        MockStorageDirectory mock_storage;
        MockClock clock;
        EventStorage storage =
            prepare_storage(mock_storage, clock, TrackingConsent::NotGranted);

        // When we write an event
        const bool ok = storage.HandleWrite("while-not-granted", {});

        // Then the write should _still_ succeed, because we've successfully handled the
        // event by dropping it in accordance with the user's tracking preferences
        REQUIRE(ok);

        // And both pending and granted subdirs should remain empty
        auto pending_relpaths = mock_storage.FindFiles("events/no-upload");
        REQUIRE(pending_relpaths.size() == 0);
        auto granted_relpaths = mock_storage.FindFiles("events/yes-upload");
        REQUIRE(granted_relpaths.size() == 0);
    }

    // TODO: Test SetTrackingConsent once Delete/MigrateTo are implemented:
    // - Verify that events previously written to pending appear in granted when consent
    //   is granted
    // - Verify that events previously written to pending are deleted when consent is
    //   revoked
    // - Verify whatever synchronization is involved in migrating pending -> granted
    // - Verify that events already in granted are not changed or removed in any case
    // - Verify that writes made after tracking consent change reflect the new value
}

class FeatureAlpha : public MockFeature
{
public:
    FeatureAlpha()
        : MockFeature(CreateFeatureId("ALFA"), "alpha")
    {
    }
};

class FeatureBravo : public MockFeature
{
public:
    FeatureBravo()
        : MockFeature(CreateFeatureId("BRVO"), "bravo")
    {
    }
};

TEST_CASE("StorageThreadMain", "[unit]")
{
    // StorageThreadMain consumes from a thread-safe blocking queue and dispatches
    // function calls to the appropriate feature-specific state in response to message.

    // In these tests, we can push the desired set of messages into the queue, then stop
    // queue processing, and then our thread entrypoint can read from the queue until
    // it's drained.

    // Common test setup code: populate a RegisteredFeature vector suitable for use by
    // the storage thread
    auto init_features = [](TrackingConsent alpha_consent,
                            TrackingConsent bravo_consent,
                            MockStorageDirectory& mock_storage,
                            MockClock& clock,
                            std::vector<RegisteredFeature>& out_features) -> void
    {
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
            CreateFeatureId("ALFA"),
            "alpha",
            std::make_shared<FeatureAlpha>(),
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
            nullptr, // event_read_directory is exclusive to upload thread
            nullptr  // upload_state is exclusive to upload thread
        );

        // "Register" Bravo
        out_features.emplace_back(
            CreateFeatureId("BRVO"),
            "bravo",
            std::make_shared<FeatureBravo>(),
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
            nullptr, // event_read_directory is exclusive to upload thread
            nullptr  // upload_state is exclusive to upload thread
        );
    };

    SECTION("M dispatch calls to appropriate features W queue contains messages")
    {
        // Given two registered features Alpha and Bravo, with initial tracking consent:
        // - Alpha (feature ID "ALFA"): NotGranted
        // - Bravo (feature ID "BRVO"): Granted
        MockStorageDirectory mock_storage;
        MockClock clock;
        std::vector<RegisteredFeature> features;
        init_features(
            TrackingConsent::NotGranted,
            TrackingConsent::Granted,
            mock_storage,
            clock,
            features
        );
        REQUIRE(features.size() == 2);

        // And a queue to which the following messages have been produced:
        // - Handle write from "ALFA" (should be ignored)
        // - Handle write from "BRVO" (should be written to granted dir)
        // - Change tracking consent to Pending for all features
        // - Handle write from "ALFA" (should be written to pending dir)
        // - Handle write from "BRVO" (should be written to pending dir)
        Queue<StorageMessage> queue;
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
