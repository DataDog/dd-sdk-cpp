// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/core/queue.hpp"
#include "datadog/impl/core/scheduler.hpp"
#include "datadog/impl/rum/timeseries/buffer.hpp"
#include "datadog/impl/rum/timeseries/pipeline.hpp"

#include "impl/rum/timeseries/fakes.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace timeseries_test;
using namespace std::chrono_literals;

namespace {

struct Noop final : Callback<void> {
  void OnSuccess() override {}
  void OnFailure(std::exception_ptr) override {}
};

// fake reader -> real Scheduler -> Pipeline -> real FeatureScope (synchronous testing
// mode, as RunOnContextThread) -> fake factory -> capturing writer
struct E2E final {
  SteppingClock clock;
  DiagnosticMessageBuffer diagnostics;
  WriterCapture capture;
  std::shared_ptr<FactoryState> factory_state = std::make_shared<FactoryState>();
  CoreContextProvider context_provider{ContextWithSession(SomeSession())};
  FeatureScope scope;
  FakeReader* reader = nullptr;
  std::shared_ptr<Pipeline<int64_t>> pipeline;
  Scheduler scheduler;

  explicit E2E(size_t buffer_size, int64_t limit)
      : scope(
            FeatureScope::CreateForTesting(
                context_provider,
                capture.Writer(),
                [](FeatureMessage) { return true; },
                diagnostics.CreateTestLogger()
            )
        ) {
    auto fake_reader = std::make_unique<FakeReader>(clock);
    reader = fake_reader.get();
    reader->limit = limit;
    pipeline = Pipeline<int64_t>::Create(
        BoundSession(),
        std::move(fake_reader),
        std::make_unique<FakeFactory>(factory_state),
        Buffer<int64_t>(buffer_size),
        std::make_shared<CollectedTimeseriesSessionRegistry>(),
        [this](ContextThreadFunc func) { scope.ExecuteOnContextThread(func); },
        diagnostics.CreateTestLogger()
    );
  }

  Scheduler::TaskId Start() {
    auto p = pipeline;
    const auto id =
        scheduler.Schedule<void>(1ms, [p] { p->Execute(); }, std::make_shared<Noop>());
    REQUIRE(id.has_value());
    return *id;
  }

  // Once the reader has seen `extra` reads past its limit, every point it will ever
  // return has been through Execute(), since runs are serial
  bool WaitUntilExhausted(int64_t limit, int64_t extra = 3) {
    return WaitFor([&] { return reader->reads.load() >= limit + extra; });
  }

  // As the collector stops a session: cancel the task, then Flush
  void StopSession(Scheduler::TaskId id) {
    CHECK(scheduler.Cancel(id));
    pipeline->Flush();
  }

  void CheckTimestampsAndSession() {
    capture.CheckCleanWrites();
    int64_t index = 0;
    for (size_t b = 0; b < factory_state->batches.size(); b++) {
      CHECK(factory_state->sessions[b].session_id == SomeSession());
      CHECK(factory_state->sessions[b].application_id == SomeApplication());
      for (size_t i = 0; i < factory_state->batches[b].size(); i++, index++) {
        // SteppingClock: the n-th reading is 1'000'000 + n ms, one reading per Read(),
        // so one per point while the reader has values
        CHECK(factory_state->batches[b][i] == index);
        CHECK(
            factory_state->timestamps[b][i] ==
            Timestamp{std::chrono::milliseconds(1'000'000 + index)}
        );
      }
    }
  }
};

}  // namespace

TEST_CASE("Pipeline end-to-end", "[unit][rum][timeseries][thread-safety]") {
  SECTION("M write exactly one event W buffer fills") {
    E2E e2e(3, 3);
    const auto id = e2e.Start();

    REQUIRE(WaitFor([&] { return e2e.capture.Count() >= 1; }));
    REQUIRE(e2e.WaitUntilExhausted(3));  // time for any duplicate to show up
    e2e.scheduler.Cancel(id);
    e2e.scheduler.Shutdown(true);

    CHECK(e2e.capture.Count() == 1);
    REQUIRE(e2e.factory_state->batches.size() == 1);
    CHECK(e2e.factory_state->batches[0] == Range(0, 3));
    e2e.CheckTimestampsAndSession();
    CHECK(e2e.diagnostics.TotalSize() == 0);
  }

  SECTION("M write the partial batch once W session stops") {
    E2E e2e(3, 2);
    const auto id = e2e.Start();

    REQUIRE(e2e.WaitUntilExhausted(2));
    e2e.StopSession(id);
    CHECK(e2e.capture.Count() == 1);

    // More points would be available now, but the pipeline is closed and cancelled
    e2e.reader->limit = -1;
    const int64_t reads_at_close = e2e.reader->reads.load();
    e2e.scheduler.Shutdown(true);
    CHECK(e2e.reader->reads.load() <= reads_at_close + 1);  // at most the in-flight run

    CHECK(e2e.capture.Count() == 1);
    REQUIRE(e2e.factory_state->batches.size() == 1);
    CHECK(e2e.factory_state->batches[0] == Range(0, 2));
    e2e.CheckTimestampsAndSession();
  }

  SECTION("M write full and partial batches in order W session stops") {
    E2E e2e(3, 5);
    const auto id = e2e.Start();

    REQUIRE(e2e.WaitUntilExhausted(5));
    e2e.StopSession(id);
    e2e.scheduler.Shutdown(true);

    CHECK(e2e.capture.Count() == 2);
    REQUIRE(e2e.factory_state->batches.size() == 2);
    CHECK(e2e.factory_state->batches[0] == Range(0, 3));
    CHECK(e2e.factory_state->batches[1] == Range(3, 2));
    e2e.CheckTimestampsAndSession();
  }

  SECTION(
      "M write batches on the context thread, in order W context thread is asynchronous"
  ) {
    SteppingClock clock;
    DiagnosticMessageBuffer diagnostics;
    WriterCapture capture;
    auto factory_state = std::make_shared<FactoryState>();
    CoreContextProvider context_provider{ContextWithSession(SomeSession())};

    // The production setup: posted functions are queued and run by a consumer thread
    Queue<std::function<void()>> context_queue;
    FeatureScope scope = FeatureScope::Create(
        context_provider,
        capture.Writer(),
        [](FeatureMessage) { return true; },
        diagnostics.CreateTestLogger(),
        context_queue
    );
    std::thread context_thread([&context_queue] {
      while (std::optional<std::function<void()>> func = context_queue.Pop()) {
        (*func)();
      }
    });
    const std::thread::id context_thread_id = context_thread.get_id();

    constexpr int64_t LIMIT = 10;
    auto fake_reader = std::make_unique<FakeReader>(clock);
    FakeReader* reader = fake_reader.get();
    reader->limit = LIMIT;
    auto pipeline = Pipeline<int64_t>::Create(
        BoundSession(),
        std::move(fake_reader),
        std::make_unique<FakeFactory>(factory_state),
        Buffer<int64_t>(3),
        std::make_shared<CollectedTimeseriesSessionRegistry>(),
        [&scope](ContextThreadFunc func) { scope.ExecuteOnContextThread(func); },
        diagnostics.CreateTestLogger()
    );

    Scheduler scheduler;
    const auto id = scheduler.Schedule<void>(
        1ms, [pipeline] { pipeline->Execute(); }, std::make_shared<Noop>()
    );
    REQUIRE(id.has_value());
    REQUIRE(WaitFor([&] { return reader->reads.load() >= LIMIT + 3; }));
    CHECK(scheduler.Cancel(*id));
    scheduler.Shutdown(true);
    pipeline->Flush();
    pipeline.reset();  // queued writes keep the pipeline alive

    REQUIRE(WaitFor([&] { return capture.Count() == 4; }));  // 3 full + 1 partial
    context_queue.Stop();
    context_thread.join();

    REQUIRE(factory_state->batches.size() == 4);
    CHECK(factory_state->batches[0] == Range(0, 3));
    CHECK(factory_state->batches[1] == Range(3, 3));
    CHECK(factory_state->batches[2] == Range(6, 3));
    CHECK(factory_state->batches[3] == Range(9, 1));
    for (const std::thread::id& writer_thread : capture.threads) {
      CHECK(writer_thread == context_thread_id);
    }
    capture.CheckCleanWrites();
    CHECK(diagnostics.TotalSize() == 0);
  }
}
