// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/rum/timeseries/buffer.hpp"
#include "datadog/impl/rum/timeseries/collected_session_registry.hpp"
#include "datadog/impl/rum/timeseries/data_points_reader.hpp"
#include "datadog/impl/rum/timeseries/event_factory.hpp"
#include "datadog/impl/rum/timeseries/pipeline.hpp"

#include "mock/clock.hpp"
#include "support/context.hpp"
#include "support/diagnostics.hpp"

namespace timeseries_test {

using namespace datadog;
using namespace datadog::impl;

/**
 * Reader returning 0, 1, 2, ... (or nullopt while `available` is false, or once
 * `limit` values have been returned). Counters are atomic so tests can inspect them
 * from other threads. `before_read`, if set (before any thread runs), is called at the
 * start of every read, e.g. to block.
 */
class FakeReader final : public DataPointsReader<int64_t> {
 public:
  explicit FakeReader(const platform::IClock& clock)
      : DataPointsReader(clock, std::chrono::milliseconds(10)) {}

  std::atomic<bool> available{true};
  std::atomic<int64_t> reads{0};
  std::atomic<int64_t> next_value{0};
  std::atomic<int64_t> limit{-1};  // negative: unlimited
  std::function<void()> before_read;

 protected:
  std::optional<int64_t> ReadValue() override {
    if (before_read) {
      before_read();
    }
    reads++;
    const int64_t max = limit.load();
    if (!available || (max >= 0 && next_value.load() >= max)) {
      return std::nullopt;
    }
    return next_value++;
  }
};

/** Clock whose n-th reading is `base + n` milliseconds; deterministic and monotonic. */
class SteppingClock final : public platform::IClock {
 public:
  Timestamp Now() const override {
    return Timestamp{std::chrono::milliseconds(1'000'000 + _ticks++)};
  }

 private:
  mutable std::atomic<int64_t> _ticks{0};
};

inline UUID SomeSession() {
  const uint8_t bytes[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  return UUID(bytes);
}

inline UUID OtherSession() {
  const uint8_t bytes[16] = {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
  return UUID(bytes);
}

inline UUID SomeApplication() {
  const uint8_t bytes[16] = {9, 9, 9, 9, 8, 8, 8, 8, 7, 7, 7, 7, 6, 6, 6, 6};
  return UUID(bytes);
}

inline TimeseriesBoundSession BoundSession() {
  return TimeseriesBoundSession{
      SomeApplication(),
      SomeSession(),
      RumSessionType::User,
      RumSessionPrecondition::InactivityTimeout,  // not the default, to be observable
  };
}

/**
 * Shared state of FakeFactory, which the pipeline owns, so tests can inspect it. The
 * mutex serializes Encode() calls from different threads; tests read the vectors only
 * after the threads that wrote them have been synchronized with.
 */
struct FactoryState final {
  std::mutex mutex;
  bool fail = false;                          // Encode returns nullopt
  bool empty_output = false;                  // Encode returns an empty view
  std::vector<std::vector<int64_t>> batches;  // values of each encoded batch
  std::vector<std::vector<Timestamp>> timestamps;
  std::vector<TimeseriesBoundSession> sessions;
  std::vector<std::optional<RumFeatureContext>> contexts;
  size_t encode_calls = 0;
};

class FakeFactory final : public EventFactory<int64_t> {
 public:
  explicit FakeFactory(std::shared_ptr<FactoryState> state)
      : _state(std::move(state)) {}

  std::optional<std::string_view> Encode(
      const CoreContext& ctx,
      const TimeseriesBoundSession& session,
      const std::vector<DataPoint<int64_t>>& points
  ) override {
    std::lock_guard lock(_state->mutex);
    _state->encode_calls++;
    _state->contexts.push_back(ctx.rum);
    if (_state->fail) {
      return std::nullopt;
    }
    if (_state->empty_output) {
      return std::string_view();
    }
    std::vector<int64_t> values;
    std::vector<Timestamp> timestamps;
    for (const auto& p : points) {
      values.push_back(p.value);
      timestamps.push_back(p.timestamp);
    }
    _state->batches.push_back(values);
    _state->timestamps.push_back(timestamps);
    _state->sessions.push_back(session);
    _encoded = "event:" + std::to_string(_state->batches.size());
    return std::string_view(_encoded);
  }

 private:
  std::shared_ptr<FactoryState> _state;
  std::string _encoded;
};

inline CoreContext ContextWithSession(UUID session_id) {
  CoreContext ctx = MOCK_CONTEXT;
  RumFeatureContext rum;
  rum.session_id = session_id;
  ctx.rum = rum;
  return ctx;
}

/**
 * Capturing writer: records every event and returns `result`. The writer may run on
 * any thread, where Catch2 assertions are not safe, so it only records facts under the
 * mutex; call CheckCleanWrites() on the main thread once the writing threads are done.
 */
struct WriterCapture final {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> events;
  std::vector<std::thread::id> threads;  // thread of each write
  bool result = true;
  size_t non_empty_metadata_writes = 0;
  size_t bypass_writes = 0;

  EventWriter Writer() {
    return [this](Block event, Block metadata, bool bypass) {
      {
        std::lock_guard lock(mutex);
        events.emplace_back(event);
        threads.push_back(std::this_thread::get_id());
        if (!metadata.empty()) {
          non_empty_metadata_writes++;
        }
        if (bypass) {
          bypass_writes++;
        }
        cv.notify_all();
      }
      return result;
    };
  }

  /** Main thread only, after the threads that wrote have been joined or waited for. */
  void CheckCleanWrites() {
    std::lock_guard lock(mutex);
    CHECK(non_empty_metadata_writes == 0);
    CHECK(bypass_writes == 0);
  }

  size_t Count() {
    std::lock_guard lock(mutex);
    return events.size();
  }
};

/** Polls `condition` until it holds or `timeout` elapses; returns whether it held. */
template <typename Pred>
bool WaitFor(
    Pred condition, std::chrono::milliseconds timeout = std::chrono::seconds(10)
) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!condition()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::yield();
  }
  return true;
}

/**
 * Wires a Pipeline<int64_t> to fakes. Posted functions run synchronously with `ctx` and
 * the capturing writer, unless `hold` is set, in which case they are queued.
 */
struct Fixture final {
  static constexpr size_t BUFFER_SIZE = 3;

  MockClock clock;
  DiagnosticMessageBuffer diagnostics;
  WriterCapture writer_capture;
  std::shared_ptr<FactoryState> factory_state = std::make_shared<FactoryState>();
  FakeReader* reader = nullptr;
  std::optional<CoreContext> ctx{ContextWithSession(SomeSession())};
  EventWriter writer = writer_capture.Writer();
  std::shared_ptr<CollectedTimeseriesSessionRegistry> collected_sessions =
      std::make_shared<CollectedTimeseriesSessionRegistry>();
  std::shared_ptr<Pipeline<int64_t>> pipeline;

  std::mutex mutex;
  bool hold = false;
  std::vector<ContextThreadFunc> held;
  std::atomic<int> posts{0};

  explicit Fixture(size_t buffer_size = BUFFER_SIZE) {
    clock.FreezeAtMilliseconds(1000);
    auto fake_reader = std::make_unique<FakeReader>(clock);
    reader = fake_reader.get();
    pipeline = Pipeline<int64_t>::Create(
        BoundSession(),
        std::move(fake_reader),
        std::make_unique<FakeFactory>(factory_state),
        Buffer<int64_t>(buffer_size),
        collected_sessions,
        [this](ContextThreadFunc func) {
          posts++;
          {
            std::lock_guard lock(mutex);
            if (hold) {
              held.push_back(std::move(func));
              return;
            }
          }
          Run(func);
        },
        diagnostics.CreateTestLogger()
    );
  }

  void Run(const ContextThreadFunc& func) {
    const CoreContext& c = ctx.has_value() ? *ctx : MOCK_CONTEXT;
    func(c, writer, [](FeatureMessage) { return true; });
  }

  void Execute(int times) {
    for (int i = 0; i < times; i++) {
      pipeline->Execute();
    }
  }

  void Flush() { pipeline->Flush(); }
};

inline std::vector<int64_t> Range(int64_t first, int64_t count) {
  std::vector<int64_t> values;
  for (int64_t i = 0; i < count; i++) {
    values.push_back(first + i);
  }
  return values;
}

}  // namespace timeseries_test
