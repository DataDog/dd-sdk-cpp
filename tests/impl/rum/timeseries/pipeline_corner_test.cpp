// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "datadog/impl/core/scheduler.hpp"
#include "datadog/impl/rum/timeseries/buffer.hpp"
#include "datadog/impl/rum/timeseries/pipeline.hpp"

#include "impl/rum/timeseries/fakes.hpp"
#include "support/allocation_tracker.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace timeseries_test;
using namespace std::chrono_literals;

namespace {
// Lets a test hold the reader inside Execute() until it chooses to release it
class Gate final {
 public:
  // Called by the reader: announces arrival and blocks until released
  void Enter() {
    std::unique_lock lock(_mutex);
    _entered = true;
    _cv.notify_all();
    _cv.wait_for(lock, 20s, [this] { return _released; });
  }

  bool WaitEntered() {
    std::unique_lock lock(_mutex);
    return _cv.wait_for(lock, 10s, [this] { return _entered; });
  }

  void Release() {
    std::lock_guard lock(_mutex);
    _released = true;
    _cv.notify_all();
  }

 private:
  std::mutex _mutex;
  std::condition_variable _cv;
  bool _entered = false;
  bool _released = false;
};

// Signals the end of every scheduled run
struct RunSignal final : Callback<void> {
  std::atomic<int> runs{0};
  void OnSuccess() override { runs++; }
  void OnFailure(std::exception_ptr) override {}
};
}  // namespace

TEST_CASE("Pipeline session end", "[unit][rum][timeseries][thread-safety]") {
  SECTION(
      "M write the batch once with the bound session W write request runs after Flush"
  ) {
    Fixture f;
    f.hold = true;
    f.Execute(3);
    REQUIRE(f.held.size() == 1);

    // The session changes under the pipeline's feet; it must not matter
    f.ctx = ContextWithSession(OtherSession());
    f.Flush();
    CHECK(f.writer_capture.Count() == 0);  // the buffer was drained into the request

    f.Run(f.held[0]);
    CHECK(f.writer_capture.Count() == 1);
    REQUIRE(f.factory_state->batches.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
    CHECK(f.factory_state->sessions[0].session_id == SomeSession());
  }

  SECTION("M add no empty event W write request runs before Flush") {
    Fixture f;
    f.hold = true;
    f.Execute(3);
    REQUIRE(f.held.size() == 1);

    f.ctx = ContextWithSession(OtherSession());
    f.Run(f.held[0]);
    REQUIRE(f.writer_capture.Count() == 1);

    f.Flush();
    CHECK(f.writer_capture.Count() == 1);  // Flush adds no empty event
    REQUIRE(f.factory_state->batches.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
    CHECK(f.factory_state->sessions[0].session_id == SomeSession());
  }

  SECTION("M write every batch in order W queued requests run before Flush") {
    Fixture f;
    f.hold = true;
    f.Execute(7);
    REQUIRE(f.held.size() == 2);

    // The context queue runs the requests posted before the session end first
    for (const auto& func : f.held) {
      f.Run(func);
    }
    f.hold = false;
    f.Flush();
    REQUIRE(f.factory_state->batches.size() == 3);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
    CHECK(f.factory_state->batches[1] == Range(3, 3));
    CHECK(f.factory_state->batches[2] == Range(6, 1));
  }

  SECTION("M write the late point W it fills the buffer after Flush") {
    // Buffer size 1: the point read after Flush() fills the buffer and is posted
    Fixture f(1);
    Gate gate;
    std::atomic<bool> first{true};
    f.reader->before_read = [&] {
      if (first.exchange(false)) {
        gate.Enter();
      }
    };

    auto signal = std::make_shared<RunSignal>();
    Scheduler scheduler;
    auto pipeline = f.pipeline;
    const auto id =
        scheduler.Schedule<void>(2ms, [pipeline] { pipeline->Execute(); }, signal);
    REQUIRE(id.has_value());
    REQUIRE(gate.WaitEntered());  // the scheduler thread is inside Execute()

    // Session stop, as the collector does it
    CHECK(scheduler.Cancel(*id));
    f.Flush();
    CHECK(f.writer_capture.Count() == 0);

    gate.Release();
    REQUIRE(WaitFor([&] { return signal->runs.load() >= 1; }));
    scheduler.Shutdown(true);

    // The late point is written, still with the bound session
    REQUIRE(f.writer_capture.Count() == 1);
    CHECK(f.factory_state->sessions[0].session_id == SomeSession());
    f.writer_capture.CheckCleanWrites();
  }

  SECTION("M keep the late point buffered W it does not fill the buffer after Flush") {
    Fixture f(3);
    f.Execute(1);  // point 0 is buffered

    Gate gate;
    std::atomic<bool> first{true};
    f.reader->before_read = [&] {
      if (first.exchange(false)) {
        gate.Enter();
      }
    };
    auto signal = std::make_shared<RunSignal>();
    Scheduler scheduler;
    auto pipeline = f.pipeline;
    const auto id =
        scheduler.Schedule<void>(2ms, [pipeline] { pipeline->Execute(); }, signal);
    REQUIRE(id.has_value());
    REQUIRE(gate.WaitEntered());

    CHECK(scheduler.Cancel(*id));
    f.Flush();
    REQUIRE(f.writer_capture.Count() == 1);

    gate.Release();
    REQUIRE(WaitFor([&] { return signal->runs.load() >= 1; }));
    scheduler.Shutdown(true);

    // Point 1 lands in the buffer after Flush(): nothing writes it unless the pipeline
    // is flushed again (accepted loss of at most one point per session end)
    CHECK(f.writer_capture.Count() == 1);
    REQUIRE(f.factory_state->batches.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 1));

    f.Flush();
    REQUIRE(f.factory_state->batches.size() == 2);
    CHECK(f.factory_state->batches[1] == Range(1, 1));  // no duplicate, in order
  }

  SECTION("M write with the bound session W ctx.rum differs") {
    Fixture f;
    f.hold = true;
    f.Execute(3);
    REQUIRE(f.held.size() == 1);

    SECTION("W ctx.rum has a new session") {
      f.ctx = ContextWithSession(OtherSession());
    }
    SECTION("W ctx.rum has a zero session") { f.ctx = ContextWithSession(UUID::Zero); }
    SECTION("W there is no RUM context") { f.ctx = MOCK_CONTEXT; }

    f.Run(f.held[0]);
    CHECK(f.writer_capture.Count() == 1);
    REQUIRE(f.factory_state->sessions.size() == 1);
    CHECK(f.factory_state->sessions[0].session_id == SomeSession());
    CHECK(f.factory_state->sessions[0].application_id == SomeApplication());
    CHECK(f.diagnostics.TotalSize() == 0);
  }

  SECTION("M release the pipeline and its batches W write requests never run") {
    Fixture f;
    f.hold = true;
    f.Execute(18);  // six full batches, no write request has run
    REQUIRE(f.held.size() == 6);

    const std::weak_ptr<Pipeline<int64_t>> weak = f.pipeline;
    f.pipeline.reset();
    CHECK_FALSE(weak.expired());  // kept alive by the queued requests

    f.held.clear();  // e.g. a stopped context queue dropping them
    CHECK(weak.expired());
    CHECK(f.writer_capture.Count() == 0);
    CHECK(f.diagnostics.TotalSize() == 0);
  }
}

TEST_CASE("Pipeline edge cases", "[unit][rum][timeseries]") {
  SECTION("M keep running W flushed") {
    Fixture f;
    f.Execute(2);
    f.Flush();
    f.Flush();  // nothing left: no empty event
    REQUIRE(f.writer_capture.Count() == 1);

    f.Execute(3);
    REQUIRE(f.writer_capture.Count() == 2);
    CHECK(f.factory_state->batches[0] == Range(0, 2));
    CHECK(f.factory_state->batches[1] == Range(2, 3));
  }

  SECTION("M write one event per point W buffer size is 1") {
    Fixture f(1);
    f.Execute(4);
    REQUIRE(f.writer_capture.Count() == 4);
    for (int64_t i = 0; i < 4; i++) {
      CHECK(f.factory_state->batches[static_cast<size_t>(i)] == Range(i, 1));
    }
  }

  SECTION("M never write an empty event W buffer size is 0") {
#if WITH_DATADOG_ASSERTS
    // A zero-capacity buffer trips DATADOG_ASSERT; exercised in builds without asserts.
    // (No SKIP(): Catch2's runtime skip needs exceptions, and the SDK may be built
    // without them)
#else
    Fixture f(0);
    f.reader->available = false;
    f.Execute(3);  // nothing read: nothing to write
    CHECK(f.writer_capture.Count() == 0);
    CHECK(f.posts.load() == 0);

    f.reader->available = true;
    f.Execute(3);
    f.Flush();
    REQUIRE(f.writer_capture.Count() == 3);
    for (const auto& batch : f.factory_state->batches) {
      CHECK_FALSE(batch.empty());
    }
#endif
  }

  SECTION("M write nothing yet W the read that would fill the buffer fails") {
    Fixture f;
    f.Execute(2);
    f.reader->available = false;
    f.Execute(1);
    CHECK(f.writer_capture.Count() == 0);
    CHECK(f.posts.load() == 0);

    f.reader->available = true;
    f.Execute(1);
    CHECK(f.writer_capture.Count() == 1);
    REQUIRE(f.factory_state->batches.size() == 1);
    CHECK(f.factory_state->batches[0] == Range(0, 3));
  }

  SECTION("M treat it as an encode failure W encoded event is empty") {
    Fixture f;
    f.factory_state->empty_output = true;
    f.Execute(6);
    CHECK(f.factory_state->encode_calls == 2);
    CHECK(f.writer_capture.Count() == 0);  // the writer is never called
    CHECK(f.diagnostics.error.size() == 2);
  }

  SECTION("M log every failure W encoding or writing fails") {
    Fixture f;

    f.factory_state->fail = true;
    f.Execute(6);
    REQUIRE(f.diagnostics.error.size() == 2);
    const std::string encode_message = f.diagnostics.error[0];

    f.factory_state->fail = false;  // recovery
    f.Execute(3);
    CHECK(f.writer_capture.Count() == 1);
    CHECK(f.diagnostics.error.size() == 2);

    f.writer_capture.result = false;
    f.Execute(6);
    REQUIRE(f.diagnostics.error.size() == 4);
    CHECK(f.diagnostics.error[2] != encode_message);

    f.factory_state->fail = true;
    f.Execute(3);
    CHECK(f.diagnostics.error.size() == 5);
  }

  SECTION("M use the clock value at read time as timestamp W clock changes") {
    Fixture f;
    const std::vector<int64_t> clock_ms = {1000, 1010, 1020};

    SECTION("W clock moves forward") {}
    SECTION("W clock goes backwards between reads") {
      // Not sorted, not equally spaced: the pipeline must pass them through as read
      const std::vector<int64_t> readings = {5000, 4000, 4500};
      for (const int64_t ms : readings) {
        f.clock.FreezeAtMilliseconds(ms);
        f.pipeline->Execute();
      }
      REQUIRE(f.factory_state->timestamps.size() == 1);
      const auto& ts = f.factory_state->timestamps[0];
      REQUIRE(ts.size() == 3);
      for (size_t i = 0; i < 3; i++) {
        CHECK(ts[i] == Timestamp{std::chrono::milliseconds(readings[i])});
      }
      CHECK(f.factory_state->batches[0] == Range(0, 3));
      return;
    }

    for (const int64_t ms : clock_ms) {
      f.clock.FreezeAtMilliseconds(ms);
      f.pipeline->Execute();
    }
    REQUIRE(f.factory_state->timestamps.size() == 1);
    const auto& ts = f.factory_state->timestamps[0];
    REQUIRE(ts.size() == 3);
    for (size_t i = 0; i < 3; i++) {
      CHECK(ts[i] == Timestamp{std::chrono::milliseconds(clock_ms[i])});
    }
  }

  SECTION("M stay independent W pipelines share a context thread and a writer") {
    MockClock clock;
    clock.FreezeAtMilliseconds(1000);
    DiagnosticMessageBuffer diagnostics;
    WriterCapture capture;
    const EventWriter writer = capture.Writer();
    const CoreContext ctx = ContextWithSession(SomeSession());
    const RunOnContextThread run_on_context_thread = [&](ContextThreadFunc func) {
      func(ctx, writer, [](FeatureMessage) { return true; });
    };

    auto make = [&](std::shared_ptr<FactoryState> state) {
      return Pipeline<int64_t>::Create(
          BoundSession(),
          std::make_unique<FakeReader>(clock),
          std::make_unique<FakeFactory>(std::move(state)),
          Buffer<int64_t>(3),
          std::make_shared<CollectedTimeseriesSessionRegistry>(),
          run_on_context_thread,
          diagnostics.CreateTestLogger()
      );
    };
    auto state_a = std::make_shared<FactoryState>();
    auto state_b = std::make_shared<FactoryState>();
    auto a = make(state_a);
    auto b = make(state_b);

    SECTION("M keep separate buffers W both are sampled") {
      a->Execute();
      a->Execute();
      b->Execute();
      CHECK(capture.Count() == 0);
      a->Execute();  // fills only a's buffer
      REQUIRE(capture.Count() == 1);
      CHECK(state_a->batches[0] == Range(0, 3));
      CHECK(state_b->batches.empty());
      b->Execute();
      b->Execute();
      REQUIRE(capture.Count() == 2);
      CHECK(state_b->batches[0] == Range(0, 3));
    }

    SECTION("M keep the other running W one is flushed") {
      a->Execute();
      a->Flush();
      b->Execute();
      b->Execute();
      b->Execute();
      REQUIRE(capture.Count() == 2);
      REQUIRE(state_a->batches.size() == 1);
      REQUIRE(state_b->batches.size() == 1);
      CHECK(state_a->batches[0] == Range(0, 1));
      CHECK(state_b->batches[0] == Range(0, 3));
    }
  }
}

TEST_CASE("Buffer::Add", "[unit][rum][timeseries]") {
  SECTION("M not allocate W adding up to capacity") {
    constexpr size_t CAPACITY = 120;
    Buffer<int64_t> buffer(CAPACITY);

    AllocationTracker tracker;
    {
      for (size_t i = 0; i < CAPACITY; i++) {
        buffer.Add(DataPoint<int64_t>{Timestamp{}, static_cast<int64_t>(i)});
      }
    }
    const AllocationTracker::Stats stats = tracker.Stop();
    if (!stats.enabled) {
      return;  // allocation tracking is disabled in this build
    }
    CHECK(stats.num_allocs == 0);
    CHECK(buffer.IsFull());
  }
}
