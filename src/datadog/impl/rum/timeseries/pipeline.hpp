// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/context.hpp"
#include "datadog/impl/core/feature_scope.hpp"
#include "datadog/impl/rum/timeseries/bound_session.hpp"
#include "datadog/impl/rum/timeseries/buffer.hpp"
#include "datadog/impl/rum/timeseries/collected_session_registry.hpp"
#include "datadog/impl/rum/timeseries/data_point.hpp"
#include "datadog/impl/rum/timeseries/data_points_reader.hpp"
#include "datadog/impl/rum/timeseries/event_factory.hpp"
#include "datadog/impl/types/assert.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Schedules a function for execution on the context thread. Callable from any thread.
 */
using RunOnContextThread = std::function<void(ContextThreadFunc)>;

/**
 * Type-erased view of a Pipeline, so pipelines of different value types can be owned
 * and driven uniformly.
 *
 * A closure scheduling Execute() on a Scheduler must hold a std::shared_ptr to the
 * pipeline: Scheduler::Cancel() does not wait for a run in progress.
 */
class IPipeline {
 public:
  virtual ~IPipeline() = default;

  /** How often Execute() should be called. */
  virtual Duration Interval() const = 0;

  /**
   * Scheduler thread. Samples one data point; once the buffer is full, posts it to the
   * context thread for writing.
   */
  virtual void Execute() = 0;

  /**
   * Posts whatever is buffered to the context thread for writing. Called at session
   * end, after the scheduled task is cancelled; a point read by an Execute() still in
   * progress lands in the buffer afterwards.
   */
  virtual void Flush() = 0;

 protected:
  IPipeline() = default;

  // Polymorphic base: copying/moving through a base reference would slice
  IPipeline(const IPipeline&) = delete;
  IPipeline& operator=(const IPipeline&) = delete;
  IPipeline(IPipeline&&) = delete;
  IPipeline& operator=(IPipeline&&) = delete;
};

/**
 * Periodically reads a value, buffers the data points, and writes one timeseries event
 * per full buffer. Every event carries the session the pipeline is bound to, whatever
 * `ctx.rum` says when it is written.
 *
 * Reading happens on the scheduler thread, outside the mutex; encoding and writing on
 * the context thread. DrainAndWrite() moves the buffered points into a posted function,
 * which keeps the pipeline alive until it runs. It posts under the mutex, so batches
 * reach the context thread in the order they were drained.
 *
 * Every encode or write failure is logged; the batch is dropped.
 */
template <typename T>
class Pipeline final : public IPipeline,
                       public std::enable_shared_from_this<Pipeline<T>> {
 public:
  static std::shared_ptr<Pipeline> Create(
      const TimeseriesBoundSession& session,
      std::unique_ptr<DataPointsReader<T>> reader,
      std::unique_ptr<EventFactory<T>> event_factory,
      Buffer<T> buffer,
      std::shared_ptr<CollectedTimeseriesSessionRegistry> collected_sessions,
      RunOnContextThread run_on_context_thread,
      DiagnosticLogger logger
  ) {
    // Not make_shared: the constructor is private
    return std::shared_ptr<Pipeline>(new Pipeline(
        session,
        std::move(reader),
        std::move(event_factory),
        std::move(buffer),
        std::move(collected_sessions),
        std::move(run_on_context_thread),
        std::move(logger)
    ));
  }

  Duration Interval() const override { return _reader->Interval(); }

  void Execute() override {
    // Outside the lock: may do I/O
    std::optional<DataPoint<T>> point = _reader->Read();
    if (!point.has_value()) {
      return;
    }
    _collected_sessions->SetTimeseriesCollected(_session.session_id);

    std::lock_guard lock(_mutex);
    _buffer.Add(std::move(*point));
    if (_buffer.IsFull()) {
      DrainAndWrite();
    }
  }

  void Flush() override {
    std::lock_guard lock(_mutex);
    DrainAndWrite();
  }

 private:
  Pipeline(
      const TimeseriesBoundSession& session,
      std::unique_ptr<DataPointsReader<T>> reader,
      std::unique_ptr<EventFactory<T>> event_factory,
      Buffer<T> buffer,
      std::shared_ptr<CollectedTimeseriesSessionRegistry> collected_sessions,
      RunOnContextThread run_on_context_thread,
      DiagnosticLogger logger
  )
      : _session(session),
        _reader(std::move(reader)),
        _event_factory(std::move(event_factory)),
        _collected_sessions(std::move(collected_sessions)),
        _run_on_context_thread(std::move(run_on_context_thread)),
        _logger(std::move(logger)),
        _buffer(std::move(buffer)) {
    DATADOG_ASSERT(_reader != nullptr, "timeseries pipeline requires a reader");
    DATADOG_ASSERT(
        _event_factory != nullptr, "timeseries pipeline requires an event factory"
    );
    DATADOG_ASSERT(
        _collected_sessions != nullptr,
        "timeseries pipeline requires a collected sessions registry"
    );
    DATADOG_ASSERT(
        _run_on_context_thread != nullptr,
        "timeseries pipeline requires a function to run on the context thread"
    );
  }

  // Requires _mutex. Moves the buffered points into a function posted to the context
  // thread, which writes them as one event. Posts nothing if the buffer is empty
  void DrainAndWrite() {
    std::vector<DataPoint<T>> data_points = _buffer.Drain();
    if (data_points.empty()) {
      return;
    }
    _run_on_context_thread(
        [self = this->shared_from_this(), data_points = std::move(data_points)](
            const CoreContext& ctx, const EventWriter& writer, const MessagePublisher&
        ) { self->Write(ctx, writer, data_points); }
    );
  }

  // Context thread only
  void Write(
      const CoreContext& ctx,
      const EventWriter& writer,
      const std::vector<DataPoint<T>>& points
  ) {
    const std::optional<std::string_view> json =
        _event_factory->Encode(ctx, _session, points);
    if (!json.has_value() || json->empty()) {
      _logger.Error("Failed to encode timeseries event; dropping batch");
      return;
    }
    const bool written = writer(*json, {}, false);
    if (!written) {
      _logger.Error("Failed to write timeseries event; dropping batch");
    }
  }

  const TimeseriesBoundSession _session;
  const std::unique_ptr<DataPointsReader<T>> _reader;
  const std::unique_ptr<EventFactory<T>> _event_factory;
  const std::shared_ptr<CollectedTimeseriesSessionRegistry> _collected_sessions;
  const RunOnContextThread _run_on_context_thread;
  const DiagnosticLogger _logger;

  std::mutex _mutex;
  Buffer<T> _buffer;  // guarded by _mutex
};

}  // namespace datadog::impl
