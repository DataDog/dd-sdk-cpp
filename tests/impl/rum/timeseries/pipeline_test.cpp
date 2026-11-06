// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/timeseries/pipeline.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "datadog/impl/core/scheduler.hpp"

#include "impl/rum/timeseries/fakes.hpp"
#include "support/diagnostics.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace timeseries_test;
using namespace std::chrono_literals;

TEST_CASE("Pipeline", "[unit][rum][timeseries]") {
  SECTION("M report the reader's interval W asked") {
    Fixture f;
    CHECK(f.pipeline->Interval() == 10ms);
  }

  SECTION("M write one event per full buffer W points are read") {
    Fixture f;

    for (int i = 0; i < 2; i++) {
      f.pipeline->Execute();
    }
    CHECK(f.writer_capture.events.empty());

    f.pipeline->Execute();
    CHECK(f.writer_capture.events.size() == 1);

    for (int i = 0; i < 6; i++) {
      f.pipeline->Execute();
    }
    REQUIRE(f.writer_capture.events.size() == 3);
    REQUIRE(f.factory_state->batches.size() == 3);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
    CHECK(f.factory_state->batches[1] == Range(3, 3));
    CHECK(f.factory_state->batches[2] == Range(6, 3));
    CHECK(f.writer_capture.events[0] == "event:1");
    CHECK(f.diagnostics.TotalSize() == 0);
  }

  SECTION("M register its session W a point is collected") {
    Fixture f;
    f.reader->available = false;
    f.Execute(1);
    CHECK_FALSE(f.collected_sessions->IsTimeseriesCollected(SomeSession()));

    f.reader->available = true;
    f.Execute(1);  // the buffer is not full yet: registered anyway
    CHECK(f.collected_sessions->IsTimeseriesCollected(SomeSession()));
    CHECK(f.writer_capture.events.empty());
  }

  SECTION("M tag the batch with its bound session W batch is written") {
    Fixture f;
    f.Execute(3);
    REQUIRE(f.factory_state->sessions.size() == 1);
    const TimeseriesBoundSession& session = f.factory_state->sessions[0];
    CHECK(session.session_id == SomeSession());
    CHECK(session.application_id == SomeApplication());
    CHECK(session.type == RumSessionType::User);
    CHECK(session.precondition == RumSessionPrecondition::InactivityTimeout);
  }

  SECTION("M write the partial batch W flushed") {
    Fixture f;

    SECTION("M write nothing W buffer is empty") {
      f.Flush();
      CHECK(f.writer_capture.events.empty());
      CHECK(f.factory_state->batches.empty());
    }

    SECTION("M write it W buffer is partially filled") {
      f.pipeline->Execute();
      f.pipeline->Execute();
      f.Flush();
      REQUIRE(f.factory_state->batches.size() == 1);
      CHECK(f.factory_state->batches[0] == Range(0, 2));
    }
  }

  SECTION("M skip the point W reader has no value") {
    Fixture f;
    f.pipeline->Execute();
    f.reader->available = false;
    f.pipeline->Execute();
    f.pipeline->Execute();
    f.pipeline->Execute();
    CHECK(f.writer_capture.events.empty());

    f.reader->available = true;
    f.pipeline->Execute();
    f.pipeline->Execute();
    REQUIRE(f.factory_state->batches.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
  }

  SECTION("M drop the batch and log W encoding fails") {
    Fixture f;
    f.factory_state->fail = true;

    for (int i = 0; i < 9; i++) {
      f.pipeline->Execute();
    }
    CHECK(f.factory_state->contexts.size() == 3);  // encode attempted each time
    CHECK(f.writer_capture.events.empty());
    CHECK(f.diagnostics.error.size() == 3);

    // Recovery: later batches are written
    f.factory_state->fail = false;
    for (int i = 0; i < 3; i++) {
      f.pipeline->Execute();
    }
    CHECK(f.writer_capture.events.size() == 1);
    CHECK(f.diagnostics.error.size() == 3);
  }

  SECTION("M drop the batch and log W writing fails") {
    Fixture f;
    f.writer_capture.result = false;

    for (int i = 0; i < 6; i++) {
      f.pipeline->Execute();
    }
    f.pipeline->Execute();
    f.Flush();
    CHECK(f.writer_capture.events.size() == 3);  // attempted each time
    CHECK(f.diagnostics.error.size() == 3);
  }

  SECTION("M write the posted batch W pipeline is released before it runs") {
    Fixture f;
    f.hold = true;
    for (int i = 0; i < 3; i++) {
      f.pipeline->Execute();
    }
    REQUIRE(f.held.size() == 1);

    f.pipeline.reset();  // the posted function keeps the pipeline alive
    f.Run(f.held[0]);
    REQUIRE(f.writer_capture.events.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
  }

  SECTION("M write its own batch W several posted functions run") {
    Fixture f;
    f.hold = true;
    for (int i = 0; i < 6; i++) {
      f.pipeline->Execute();
    }
    REQUIRE(f.held.size() == 2);

    f.Run(f.held[0]);
    REQUIRE(f.factory_state->batches.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 3));

    f.Run(f.held[1]);
    REQUIRE(f.factory_state->batches.size() == 2);
    CHECK(f.factory_state->batches[1] == Range(3, 3));
  }

  SECTION("M post batches W driven by a real Scheduler") {
    Fixture f(2);
    std::atomic<bool> posted{false};
    struct Noop final : Callback<void> {
      void OnSuccess() override {}
      void OnFailure(std::exception_ptr) override {}
    };

    {
      Scheduler scheduler;
      auto pipeline = f.pipeline;
      const auto id = scheduler.Schedule<void>(
          5ms, [pipeline] { pipeline->Execute(); }, std::make_shared<Noop>()
      );
      REQUIRE(id.has_value());

      const auto deadline = std::chrono::steady_clock::now() + 10s;
      while (f.posts.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
      }
      posted = f.posts.load() > 0;
      scheduler.Shutdown(true);
    }

    REQUIRE(posted);
    // Synchronous posting wrote on the scheduler thread; Shutdown joined it
    REQUIRE_FALSE(f.factory_state->batches.empty());
    f.writer_capture.CheckCleanWrites();
    CHECK(f.factory_state->batches[0] == Range(0, 2));
  }
}

TEST_CASE("Pipeline thread-safety", "[unit][rum][timeseries][thread-safety]") {
  SECTION(
      "M keep points in order without loss W Flush runs concurrently with Execute"
  ) {
    Fixture f(7);
    f.hold = true;  // posted batches are collected, then run on the "context thread"
    constexpr int ITERATIONS = 20000;

    // Stands in for the context thread: the factory is only used under this lock
    std::mutex context_mutex;
    auto drain_posted = [&] {
      std::vector<ContextThreadFunc> funcs;
      {
        std::lock_guard lock(f.mutex);
        funcs.swap(f.held);
      }
      for (const auto& func : funcs) {
        std::lock_guard lock(context_mutex);
        f.Run(func);
      }
    };

    std::atomic<bool> done{false};
    std::thread executor([&] {
      for (int i = 0; i < ITERATIONS; i++) {
        f.pipeline->Execute();
        drain_posted();
      }
      done = true;
    });
    std::thread flusher([&] {
      while (!done) {
        std::lock_guard lock(context_mutex);
        f.Flush();
      }
    });
    executor.join();
    flusher.join();

    f.Flush();
    drain_posted();

    // Batches are written serially, so across all of them the values must be strictly
    // increasing (that also rules out duplicates), and none may be missing
    size_t total = 0;
    int64_t previous = -1;
    bool increasing = true;
    for (const auto& batch : f.factory_state->batches) {
      CHECK_FALSE(batch.empty());
      for (const int64_t value : batch) {
        increasing = increasing && value > previous;
        previous = value;
        total++;
      }
    }
    CHECK(increasing);
    CHECK(total == static_cast<size_t>(ITERATIONS));
    CHECK(previous == ITERATIONS - 1);
  }
}
