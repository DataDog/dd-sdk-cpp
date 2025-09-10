#include "core/feature.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <sstream>
#include <vector>

#include "core/tlv.hpp"
#include "mock/feature.hpp"
#include "mock/filesystem.hpp"
#include "mock/tlv.hpp"
#include "support/core.hpp"
#include "support/threading.hpp"

using namespace datadog::impl;

TEST_CASE("CreateFeatureId", "[unit]") {
  SECTION("M encode standard FourCC W given four ASCII bytes") {
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

TEST_CASE("BatchReader", "[unit]") {
  SECTION("M read event blocks W file is valid") {
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
    auto block_0_res = reader.ReadNext();

    // Then we should get the data for that block
    REQUIRE(block_0_res.has_value());
    std::optional<TLVBlock> block_0 = *block_0_res;
    REQUIRE(block_0.has_value());
    REQUIRE(block_0->type == TLVBlockType::Event);
    REQUIRE(block_0->data == "Hello");

    // And our buffer should be used as the underlying storage for that block data
    REQUIRE(buffer.capacity() >= 5);
    REQUIRE(std::string_view{buffer.data(), 5} == "Hello");

    // And: When we read the next block
    auto block_1_res = reader.ReadNext();

    // Then we should get the data for that block
    REQUIRE(block_1_res.has_value());
    std::optional<TLVBlock> block_1 = *block_1_res;
    REQUIRE(block_1->type == TLVBlockType::Event);
    REQUIRE(block_1->data == "hi");

    // And the data should be written to the same reusable buffer, clobbering the
    // data for the first block
    REQUIRE(buffer.capacity() >= 5);
    REQUIRE(std::string_view{buffer.data(), 2} == "hi");
    REQUIRE(std::string_view{buffer.data() + 2, 3} == "llo");

    // And: When we attempt to read the next block
    auto block_2_res = reader.ReadNext();

    // Then we get nullopt instead of a block (with no error), since we're at EOF
    REQUIRE(block_2_res.has_value());
    std::optional<TLVBlock> block_2 = *block_2_res;
    REQUIRE(!block_2.has_value());
  }

  SECTION("M read all blocks W file contains metadata + event blocks") {
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
    for (int i = 0; i < 4; i++) {
      auto block_res = reader.ReadNext();

      // Then we get the expected results from each read
      REQUIRE(block_res.has_value());
      auto block = *block_res;
      REQUIRE(block.has_value());
      REQUIRE(
          block->type == (i % 2 == 0 ? TLVBlockType::Metadata : TLVBlockType::Event)
      );
      s += block->data;
    }

    // And reading once more would give us nullopt to indicate EOF
    auto res = reader.ReadNext();
    REQUIRE(res.has_value());
    REQUIRE(*res == std::nullopt);

    // And we have the expected data once we're done reading
    REQUIRE(s == "metadata-0event-0metadata-1event-1");
  }

  SECTION("M return IOError W file read fails due to low-level filesystem error") {
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
    auto block_0_res = reader.ReadNext();

    // Then we get the data for that block
    REQUIRE(block_0_res.has_value());
    auto block_0 = *block_0_res;
    REQUIRE(block_0->type == TLVBlockType::Event);
    REQUIRE(block_0->data == "event-0");

    // Next: Given external conditions that prevent file reads
    storage.Corrupt("foo");

    // When we attempt to read the next block
    auto block_1_res = reader.ReadNext();

    // Then we get an error indicating the file couldn't be read
    REQUIRE(!block_1_res.has_value());
    REQUIRE(block_1_res.error() == BatchReadError::IOError);
  }

  SECTION("M return FailedRead W file read fails due to invalid file state") {
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
    auto block_0_res = reader.ReadNext();
    REQUIRE(block_0_res.has_value());
    auto block_0 = *block_0_res;
    REQUIRE(block_0->type == TLVBlockType::Event);
    REQUIRE(block_0->data == "event-0");

    // When we attempt a read operation that will fail due to issues with our handle
    storage.SetFail("foo", true);
    auto block_1_res = reader.ReadNext();

    // Then we get an error indicating the read operation failed
    REQUIRE(!block_1_res.has_value());
    REQUIRE(block_1_res.error() == BatchReadError::FailedRead);
  }

  SECTION("M return InvalidBlockFormat W file contains a TLV header w/o data") {
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
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);
  }

  SECTION("M return InvalidBlockFormat W file has TLV header indicating zero size") {
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
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);
  }

  SECTION("M return InvalidBlockFormat W file has TLV header indicating zero size") {
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
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);
  }

  SECTION("M return InvalidBlockFormat W file contains non-TLV data") {
    // Given a mock file that does not contain valid TLV data
    MockStorageDirectory storage;
    storage.WithExistingFile("foo", "this is not TLV-encoded binary data");
    auto infile = storage.OpenForRead("foo");
    REQUIRE(infile.has_value());
    std::vector<char> buffer;
    BatchReader reader(*infile.value(), buffer);

    // When we attempt to read from that ifle
    auto block_res = reader.ReadNext();

    // Then we get an error indicating the file contents are not valid TLV
    REQUIRE(!block_res.has_value());
    REQUIRE(block_res.error() == BatchReadError::InvalidBlockFormat);
  }

  SECTION("M return nullopt W file is empty") {
    // Given a mock file that is entirely empty
    MockStorageDirectory storage;
    storage.WithExistingFile("foo", "");
    auto infile = storage.OpenForRead("foo");
    REQUIRE(infile.has_value());
    std::vector<char> buffer;
    BatchReader reader(*infile.value(), buffer);

    // When we attempt to read from that ifle
    auto block_res = reader.ReadNext();

    // Then we get a successful read result with a nullopt value, indicating that we've
    // reached EOF
    REQUIRE(block_res.has_value());
    REQUIRE(*block_res == std::nullopt);
  }

  SECTION("M reuse buffer W multiple reads") {
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
    auto block_res = reader.ReadNext();
    REQUIRE(block_res.has_value());
    auto block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 1024);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'a') == 1024);

    // And we read the block with 'b' x 768
    block_res = reader.ReadNext();
    REQUIRE(block.has_value());
    block = *block_res;
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
    block_res = reader.ReadNext();
    REQUIRE(block.has_value());
    block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 16380);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'c') == 16380);

    // Then the buffer's allocation strategy should be smart enough to jump to the
    // next power of two, which is only a few bytes away (this also an
    // implementation detail; see QuantizeBufferSize)
    const size_t capacity_c = buffer.capacity();
    REQUIRE(capacity_c >= 16384);

    // Next: When we read the block with 'd' x 16384
    block_res = reader.ReadNext();
    REQUIRE(block.has_value());
    block = *block_res;
    REQUIRE(block->type == TLVBlockType::Event);
    REQUIRE(block->data.size() == 16384);
    REQUIRE(std::count(block->data.begin(), block->data.end(), 'd') == 16384);

    // Then the buffer should not have been reallocated due to the tiny difference
    // in size (another implementation detail)
    REQUIRE(buffer.capacity() == capacity_c);

    // And one final read should hit EOF
    block_res = reader.ReadNext();
    REQUIRE(block_res.has_value());
    REQUIRE(*block_res == std::nullopt);
  }
}

class CoolFeature : public MockFeature {
 public:
  CoolFeature() : MockFeature(CreateFeatureId("COOL"), "coolfeature") {}
};

class ChattyFeature : public MockFeature {
 public:
  ChattyFeature() : MockFeature(CreateFeatureId("HIHI"), "chatty") {}

  virtual void Start() override {
    // Start() should be called once it's OK to write events
    WriteEvent("hello");
  }

  virtual void Stop() override {
    // Stop() should be called before it stops being OK to write events
    WriteEvent("goodbye");
  }
};

TEST_CASE("Feature", "[unit]") {
  SECTION("M produce events to storage W WriteEvent is called") {
    // Given a running core with a registered feature
    const bool flush_http_requests = false;
    CoreTestHarness test = CoreTestHarness::Init(flush_http_requests);
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

  SECTION("M generate events only W core is running") {
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

  SECTION("M be able to produce events W core is started or stopping") {
    // Given an initialized core with a registered feature that generates events in
    // response to core start and stop
    const bool flush_http_requests = false;
    CoreTestHarness test = CoreTestHarness::Init(flush_http_requests);
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

  SECTION("M upload batches W events are produced") {
    // Given a core with our ChattyFeature
    const bool flush_http_requests = true;
    CoreTestHarness test = CoreTestHarness::Init(flush_http_requests);
    auto feature = std::make_shared<ChattyFeature>();
    REQUIRE(test.core.RegisterFeature(feature));

    // When the core is started, we generate an event, and the core is stopped;
    // causing all events to be flushed to disk, followed by a synchronous upload
    // cycle to upload those events
    REQUIRE(test.core.Start());
    REQUIRE(feature->GenerateEvent("nice weather today"));
    test.core.Stop();

    // Then core should have successfully uploaded our feature
    REQUIRE(test.client.requests.size() == 1);
    REQUIRE(test.storage.GetNumFilesDeleted() == 1);
    const MockHttpRequest& req = test.client.requests.front();
    REQUIRE(!req.aborted);
    REQUIRE(req.body == "hello,nice weather today,goodbye");

    // And the batch file should have been deleted on successful upload
    std::vector<std::string> relpaths = test.storage.FindFiles("chatty/v1");
    REQUIRE(relpaths.size() == 0);
  }

  SECTION("M upload multiple batches W many events are produced") {
    // Given a core configured to flush HTTP requests on stop
    CoreTestHarness test = CoreTestHarness::Init();
    auto feature = std::make_shared<CoolFeature>();
    REQUIRE(test.core.RegisterFeature(feature));

    // When the core is started, we generate 1024 events, and the core is stopped
    REQUIRE(test.core.Start());
    for (size_t i = 0; i < 1024; i++) {
      REQUIRE(feature->GenerateEvent("event-" + std::to_string(i)));
    }
    test.core.Stop();

    // Then core should have successfully uploaded 3 batches for our feature, since
    // the default limit is 500 events per batch
    REQUIRE(test.client.requests.size() == 3);
    REQUIRE(test.storage.GetNumFilesDeleted() == 3);

    // And those requests should encode our first 500 events, the next 500 events,
    // and then the final 24
    auto events_from = [](int base, int n) {
      std::ostringstream oss;
      for (int i = 0; i < n; i++) {
        if (i > 0) {
          oss << ',';
        }
        oss << "event-";
        oss << (base + i);
      }
      return oss.str();
    };
    REQUIRE(test.client.requests[0].body == events_from(0, 500));
    REQUIRE(test.client.requests[1].body == events_from(500, 500));
    REQUIRE(test.client.requests[2].body == events_from(1000, 24));
  }
}

TEST_CASE("Feature thread-safety", "[unit][core][thread-safety]") {
  SECTION("M handle events W produced from multiple threads concurrently") {
    // Given a started core with a registered feature
    CoreTestHarness test = CoreTestHarness::Init();
    auto feature = std::make_shared<CoolFeature>();
    REQUIRE(test.core.RegisterFeature(feature));
    REQUIRE(test.core.Start());

    // When we kick off 50 threads that each generate 100 events
    auto threads = RunParallel(50, [&](size_t thread_id) {
      std::string thread_pad = thread_id < 10 ? "0" : "";
      std::string prefix = thread_pad + std::to_string(thread_id) + ":";
      for (size_t i = 0; i < 100; i++) {
        std::string pad = i < 10 ? "0" : "";
        std::string message = prefix + pad + std::to_string(i);
        feature->GenerateEvent(message);
      }
    });

    // And we concurrently write 100 events from the main thread
    for (size_t i = 0; i < 100; i++) {
      std::string pad = i < 10 ? "0" : "";
      feature->GenerateEvent("main:" + pad + std::to_string(i));
    }

    // And then we wait for those threads to exit and then stop the core
    for (auto& thread : threads) {
      thread.join();
    }
    test.core.Stop();

    // Then exactly 5100 events should have been uploaded across all those requests
    std::vector<std::string> events;
    events.reserve(5100);
    for (MockHttpRequest& req : test.client.requests) {
      size_t start = 0;
      size_t end = req.body.find(',');
      while (end != std::string::npos) {
        events.push_back(req.body.substr(start, end - start));
        start = end + 1;
        end = req.body.find(',', start);
      }
      events.push_back(req.body.substr(start));
    }
    REQUIRE(events.size() == 5100);

    // And our events should be represented exactly
    std::sort(events.begin(), events.end());
    REQUIRE(events[0] == "00:00");
    REQUIRE(events[1] == "00:01");
    REQUIRE(events[100] == "01:00");
    REQUIRE(events[1337] == "13:37");
    REQUIRE(events[2345] == "23:45");
    REQUIRE(events[4999] == "49:99");
    REQUIRE(events[5000] == "main:00");
    REQUIRE(events[5099] == "main:99");
  }
}
