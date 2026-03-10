// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/feature.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <vector>

#include "mock/feature.hpp"
#include "mock/tlv.hpp"
#include "support/core.hpp"
#include "support/threading.hpp"

using namespace datadog::impl;

class CoolFeature : public MockFeature {
 public:
  CoolFeature() : MockFeature(CreateFeatureId("COOL"), "coolfeature") {}
};

class ChattyFeature : public MockFeature {
 public:
  ChattyFeature() : MockFeature(CreateFeatureId("HIHI"), "chatty") {}

  virtual void Start() override {
    _scope->ExecuteOnContextThread(
        [](const CoreContext&, const impl::EventWriter& writer) { writer("hello", {}); }
    );
  }

  virtual void Stop() override {
    _scope->ExecuteOnContextThread([](const CoreContext&,
                                      const impl::EventWriter& writer) {
      writer("goodbye", {});
    });
  }
};

TEST_CASE("Feature", "[unit]") {
  SECTION(
      "M produce events to storage W FeatureScope::ExecuteOnContextThread is called"
  ) {
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
