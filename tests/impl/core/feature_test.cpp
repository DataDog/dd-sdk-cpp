#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/feature.hpp"
#include "core/tlv.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"
#include "support/core.hpp"

using namespace datadog::impl;

TEST_CASE("CreateFeatureId", "[unit]")
{
    SECTION("M encode standard FourCC W given four ASCII bytes")
    {
        // Should encode with leftmost character as least significant byte
        REQUIRE(CreateFeatureId("ABCD") == 0x44434241);

        REQUIRE(CreateFeatureId("TEST") == 0x54534554);
        REQUIRE(CreateFeatureId("LOGS") == 0x53474f4C);
        REQUIRE(CreateFeatureId("1234") == 0x34333231);
        REQUIRE(CreateFeatureId("L\0GS") == 0x5347004C);
        REQUIRE(CreateFeatureId("\0\0\0\0") == 0x00000000);
        REQUIRE(CreateFeatureId("A1@#") == 0x23403141);
    }
}

TEST_CASE("BatchReader", "[unit]")
{
    SECTION("M read event blocks W file is valid")
    {
        // Given a mock file with two valid TLV event blocks
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendEvent(Block{"Hello"})
            .AppendEvent(Block{"hi"})
            .WriteTo(storage, "hello.dat");

        auto infile = storage.OpenForRead("hello.dat");
        REQUIRE(infile.has_value());

        // And a reusable read buffer
        std::vector<char> buffer;

        // And a BatchReader initialized to read from that file into that buffer
        BatchReader reader(*infile.value(), buffer);

        // When we read the first block
        auto block_0 = reader.ReadNext();

        // Then we should get the data for that block
        REQUIRE(block_0.has_value());
        REQUIRE(block_0->eof == false);
        REQUIRE(block_0->type == TLVBlockType::Event);
        REQUIRE(block_0->data == "Hello");

        // And our buffer should be used as the underlying storage for that block data
        REQUIRE(buffer.capacity() >= 5);
        REQUIRE(std::string_view{buffer.data(), 5} == "Hello");

        // And: When we read the next block
        auto block_1 = reader.ReadNext();

        // Then we should get the data for that block, and we should be at EOF
        REQUIRE(block_1.has_value());
        REQUIRE(block_1->eof == true);
        REQUIRE(block_1->type == TLVBlockType::Event);
        REQUIRE(block_1->data == "hi");

        // And the data should be written to the same reusable buffer, clobbering the
        // data for the first block
        REQUIRE(buffer.capacity() >= 5);
        REQUIRE(std::string_view{buffer.data(), 2} == "hi");
        REQUIRE(std::string_view{buffer.data() + 2, 3} == "llo");
    }

    SECTION("M read all blocks W file contains metadata + event blocks")
    {
        // Given a mock file with two valid TLV event blocks
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendMetadata(Block{"metadata-0"})
            .AppendEvent(Block{"event-0"})
            .AppendMetadata(Block{"metadata-1"})
            .AppendEvent(Block{"event-1"})
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());

        // And a BatchReader
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we read until EOF and concatenate everything into a string
        std::string s;
        s.reserve(128);
        for (int i = 0; i < 4; i++)
        {
            auto block = reader.ReadNext();

            // Then we get the expected results from each read
            REQUIRE(block.has_value());
            REQUIRE(block->eof == (i == 3));
            REQUIRE(
                block->type ==
                (i % 2 == 0 ? TLVBlockType::Metadata : TLVBlockType::Event)
            );
            s += block->data;
        }

        // And we have the expected data once we're done reading
        REQUIRE(s == "metadata-0event-0metadata-1event-1");
    }

    SECTION("M return IOError W file read fails due to low-level filesystem error")
    {
        // Given a mock file with two valid TLV event blocks
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendEvent(Block{"event-0"})
            .AppendEvent(Block{"event-1"})
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());

        // And a BatchReader
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we read the first block under normal conditions
        auto block_0 = reader.ReadNext();

        // Then we get the data for that block
        REQUIRE(block_0.has_value());
        REQUIRE(block_0->eof == false);
        REQUIRE(block_0->type == TLVBlockType::Event);
        REQUIRE(block_0->data == "event-0");

        // Next: Given external conditions that prevent file reads
        storage.Corrupt("foo");

        // When we attempt to read the next block
        auto block_1 = reader.ReadNext();

        // Then we get an error indicating the file couldn't be read
        REQUIRE(!block_1.has_value());
        REQUIRE(block_1.error() == BatchReadError::IOError);
    }

    SECTION("M return FailedRead W file read fails due to invalid file state")
    {
        // Given a mock file with two valid TLV event blocks
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendEvent(Block{"event-0"})
            .AppendEvent(Block{"event-1"})
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // And normal conditions that have allowed us to read the first block
        auto block_0 = reader.ReadNext();
        REQUIRE(block_0.has_value());
        REQUIRE(block_0->eof == false);
        REQUIRE(block_0->type == TLVBlockType::Event);
        REQUIRE(block_0->data == "event-0");

        // When we attempt a read operation that will fail due to issues with our handle
        storage.SetFail("foo", true);
        auto block_1 = reader.ReadNext();

        // Then we get an error indicating the read operation failed
        REQUIRE(!block_1.has_value());
        REQUIRE(block_1.error() == BatchReadError::FailedRead);
    }

    SECTION("M return InvalidBlockFormat W file contains a TLV header w/o data")
    {
        // Given a mock file with a header that indicates 32 bytes of data to follow,
        // but no actual data after the header
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendHeader(impl::TLVBlockHeader{TLVBlockType::Event, 32})
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we attempt to read the next block
        auto block = reader.ReadNext();

        // Then we get an error indicating the file contents are not valid TLV
        REQUIRE(!block.has_value());
        REQUIRE(block.error() == BatchReadError::InvalidBlockFormat);
    }

    SECTION("M return InvalidBlockFormat W file has TLV header indicating zero size")
    {
        // Given a mock file containing an otherwise well-formed TLV block that encodes
        // the block type with a value that does not correspond to a known TLVBlockType
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendHeader(impl::TLVBlockHeader{static_cast<TLVBlockType>(0x0002), 7})
            .AppendBytes("event-0")
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we attempt to read from that file
        auto block = reader.ReadNext();

        // Then we get an error indicating the file contents are not valid TLV
        REQUIRE(!block.has_value());
        REQUIRE(block.error() == BatchReadError::InvalidBlockFormat);
    }

    SECTION("M return InvalidBlockFormat W file has TLV header indicating zero size")
    {
        // Given a mock file containing an otherwise well-formed TLV block that shows a
        // length of zero for its value
        MockStorageDirectory storage;
        MockTLVFile()
            .AppendHeader(impl::TLVBlockHeader{TLVBlockType::Event, 0})
            .AppendBytes("event-0")
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we attempt to read from that file
        auto block = reader.ReadNext();

        // Then we get an error indicating the file contents are not valid TLV
        REQUIRE(!block.has_value());
        REQUIRE(block.error() == BatchReadError::InvalidBlockFormat);
    }

    SECTION("M return InvalidBlockFormat W file contains non-TLV data")
    {
        // Given a mock file that does not contain valid TLV data
        MockStorageDirectory storage;
        storage.WithExistingFile("foo", "this is not TLV-encoded binary data");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we attempt to read from that ifle
        auto block = reader.ReadNext();

        // Then we get an error indicating the file contents are not valid TLV
        REQUIRE(!block.has_value());
        REQUIRE(block.error() == BatchReadError::InvalidBlockFormat);
    }

    SECTION("M return InvalidBlockFormat W file is empty")
    {
        // Given a mock file that is entirely empty
        MockStorageDirectory storage;
        storage.WithExistingFile("foo", "");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());
        std::vector<char> buffer;
        BatchReader reader(*infile.value(), buffer);

        // When we attempt to read from that ifle
        auto block = reader.ReadNext();

        // Then we get an error indicating the file contents are not valid TLV:
        // BatchReader is only in the business of reading batches, and a zero-length
        // batch and should not be written to disk
        REQUIRE(!block.has_value());
        REQUIRE(block.error() == BatchReadError::InvalidBlockFormat);
    }

    SECTION("M reuse buffer W multiple reads")
    {
        // Given a bunch of large-ish string values that repeat the same byte N times
        std::string value_a_1024(1024, 'a');
        std::string value_b_768(768, 'b');
        std::string value_c_16380(16380, 'c');
        std::string value_d_16384(16384, 'd');

        // And a reusable buffer in which we've initially reserved 256 bytes
        std::vector<char> buffer;
        buffer.reserve(256);

        // And a mock file that contains valid TLV event blocks for each string
        MockStorageDirectory storage;
        MockTLVFile(4096)
            .AppendEvent(value_a_1024)
            .AppendEvent(value_b_768)
            .AppendEvent(value_c_16380)
            .AppendEvent(value_d_16384)
            .WriteTo(storage, "foo");
        auto infile = storage.OpenForRead("foo");
        REQUIRE(infile.has_value());

        // And a reader initialized to use our buffer
        BatchReader reader(*infile.value(), buffer);
        REQUIRE(buffer.capacity() == 256);

        // When we read the block with 'a' x 1024
        auto block = reader.ReadNext();
        REQUIRE(block.has_value());
        REQUIRE(!block->eof);
        REQUIRE(block->type == TLVBlockType::Event);
        REQUIRE(block->data.size() == 1024);
        REQUIRE(std::count(block->data.begin(), block->data.end(), 'a') == 1024);

        // And we read the block with 'b' x 768
        block = reader.ReadNext();
        REQUIRE(block.has_value());
        REQUIRE(!block->eof);
        REQUIRE(block->type == TLVBlockType::Event);
        REQUIRE(block->data.size() == 768);
        REQUIRE(std::count(block->data.begin(), block->data.end(), 'b') == 768);

        // Then the underlying buffer should have been reallocated to fit at least 1024
        // bytes, and block B should have overwritten the first 768 bytes of block A
        REQUIRE(buffer.size() == 768);
        REQUIRE(buffer.capacity() >= 1024);
        REQUIRE(std::count(buffer.begin(), buffer.begin() + 1024, 'b') == 768);
        REQUIRE(std::count(buffer.begin(), buffer.begin() + 1024, 'a') == 256);
        REQUIRE(std::string_view{buffer.data(), 1024}.find('a') == 768);

        // And the buffer's allocation strategy should be conservative enough not to
        // jump straight to the order of ~16kb when we're working with ~1kb values
        // Note: This is an implementation detail, but a fair assumption to codify
        REQUIRE(buffer.capacity() < 16384);

        // Next: When we read the block with 'c' x 16380
        block = reader.ReadNext();
        REQUIRE(block.has_value());
        REQUIRE(!block->eof);
        REQUIRE(block->type == TLVBlockType::Event);
        REQUIRE(block->data.size() == 16380);
        REQUIRE(std::count(block->data.begin(), block->data.end(), 'c') == 16380);

        // Then the buffer's allocation strategy should be smart enough to jump to the
        // next power of two, which is only a few bytes away (this also an
        // implementation detail; see QuantizeBufferSize)
        const size_t capacity_c = buffer.capacity();
        REQUIRE(capacity_c >= 16384);

        // Next: When we read the block with 'd' x 16384
        block = reader.ReadNext();
        REQUIRE(block.has_value());
        REQUIRE(block->eof);
        REQUIRE(block->type == TLVBlockType::Event);
        REQUIRE(block->data.size() == 16384);
        REQUIRE(std::count(block->data.begin(), block->data.end(), 'd') == 16384);

        // Then the buffer should not have been reallocated due to the tiny difference
        // in size (another implementation detail)
        REQUIRE(buffer.capacity() == capacity_c);
    }
}

class CoolFeature : public MockFeature
{
public:
    CoolFeature()
        : MockFeature(CreateFeatureId("COOL"), "coolfeature")
    {
    }
};

class ChattyFeature : public MockFeature
{
public:
    ChattyFeature()
        : MockFeature(CreateFeatureId("HIHI"), "chatty")
    {
    }

    virtual void Start() override
    {
        // Start() should be called once it's OK to write events
        WriteEvent("hello");
    }

    virtual void Stop() override
    {
        // Stop() should be called before it stops being OK to write events
        WriteEvent("goodbye");
    }
};

TEST_CASE("Feature", "[unit]")
{
    SECTION("M produce events to storage W WriteEvent is called")
    {
        // Given a running core with a registered feature
        CoreTestHarness test = CoreTestHarness::Init();
        test.clock.FreezeAtMilliseconds(1708675309000);
        auto feature = std::make_shared<CoolFeature>();
        REQUIRE(test.core.RegisterFeature(feature));
        REQUIRE(test.core.Start());

        // When we generate a few events
        REQUIRE(feature->GenerateEvent("event-0"));
        REQUIRE(feature->GenerateEvent("event-1", "metadata-1"));
        REQUIRE(feature->GenerateEvent("event-2"));

        // And stop the core, allowing it to drain the queue and flush to storage
        test.core.Stop();

        // Then our mock filesystem should contain a single batch file in the storage
        // directory for our feature
        std::vector<std::string> relpaths = test.storage.FindFiles("coolfeature/v1");
        REQUIRE(relpaths.size() == 1);
        REQUIRE(relpaths.front() == "coolfeature/v1/1708675309000");

        // And that file should contain the event data and metadata that our feature
        // generated, batched and encoded in TLV format
        std::string expected = MockTLVFile()
                                   .AppendEvent("event-0")
                                   .AppendMetadata("metadata-1")
                                   .AppendEvent("event-1")
                                   .AppendEvent("event-2")
                                   .ToString();
        REQUIRE(test.storage.Cat(relpaths.front()) == expected);
    }

    SECTION("M generate events only W core is running")
    {
        // Given a core with a registered feature
        CoreTestHarness test = CoreTestHarness::Init();
        auto feature = std::make_shared<CoolFeature>();
        REQUIRE(test.core.RegisterFeature(feature));

        // When we attempt to write an event before core start
        bool ok = feature->GenerateEvent("before-start");
        // Then it's ignored
        REQUIRE(!ok);

        // And: When we start the core and then attempt to write an event
        REQUIRE(test.core.Start());
        ok = feature->GenerateEvent("after-start");
        // Then it's accepted
        REQUIRE(ok);

        // And: When we stop the core and then attempt to write an event
        test.core.Stop();
        ok = feature->GenerateEvent("after-stop");
        // Then it's ignored
        REQUIRE(!ok);
    }

    SECTION("M be able to produce events W core is started or stopping")
    {
        // Given an initialized core with a registered feature that generates events in
        // response to core start and stop
        CoreTestHarness test = CoreTestHarness::Init();
        auto feature = std::make_shared<ChattyFeature>();
        REQUIRE(test.core.RegisterFeature(feature));

        // When the core is started, we generate an event, and the core is stopped
        REQUIRE(test.core.Start());
        REQUIRE(feature->GenerateEvent("nice weather today"));
        test.core.Stop();

        // Then the resulting batch file should contain all events, including those
        // generated on start and on stop, in the correct order
        std::vector<std::string> relpaths = test.storage.FindFiles("chatty/v1");
        REQUIRE(relpaths.size() == 1);
        std::string expected = MockTLVFile()
                                   .AppendEvent("hello")
                                   .AppendEvent("nice weather today")
                                   .AppendEvent("goodbye")
                                   .ToString();
        REQUIRE(test.storage.Cat(relpaths.front()) == expected);
    }
}
