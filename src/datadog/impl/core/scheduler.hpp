// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "datadog/timestamp.hpp"

namespace datadog::impl {

/**
 * Receives the outcome of every run of a scheduled task. Both methods are called on the
 * scheduler thread; an exception escaping either of them terminates the program.
 *
 * Common part of Callback<T> and Callback<void>, which differ only in OnSuccess.
 */
class CallbackBase {
 public:
  CallbackBase() = default;
  // Non-copyable and non-movable: copying through a base reference would slice off the
  // derived listener's state. Callbacks are shared via std::shared_ptr, never copied.
  CallbackBase(const CallbackBase&) = delete;
  CallbackBase& operator=(const CallbackBase&) = delete;
  CallbackBase(CallbackBase&&) = delete;
  CallbackBase& operator=(CallbackBase&&) = delete;
  virtual ~CallbackBase() = default;

  /**
   * Called with the exception thrown by a run. Never called when the SDK is built
   * without exception support.
   */
  virtual void OnFailure(std::exception_ptr error) = 0;
};

template <typename T>
class Callback : public CallbackBase {
 public:
  using Task = std::function<T()>;

  /** Called with the task's result after each run that returns normally. */
  virtual void OnSuccess(T result) = 0;
};

template <>
class Callback<void> : public CallbackBase {
 public:
  using Task = std::function<void()>;

  /** Called after each run that returns normally. */
  virtual void OnSuccess() = 0;
};

/**
 * Runs periodic tasks on a dedicated background thread.
 *
 * The thread sleeps on a condition variable until the earliest task is due (or until
 * a task is added/cancelled, or the scheduler is shut down), so it never busy-waits.
 * Tasks run serially on the scheduler thread, outside of any internal lock, and use a
 * monotonic clock so wall-clock changes don't affect their cadence.
 *
 * Schedule() and Cancel() are thread-safe. Shutdown() must not be called concurrently
 * with itself or with the destructor.
 */
class Scheduler {
 public:
  using TaskId = std::uint64_t;

  /** Longest accepted period; keeps deadline arithmetic far from clock overflow. */
  static constexpr Duration MAX_PERIOD = std::chrono::hours(24 * 365);

  /** Starts the scheduler thread. */
  Scheduler();

  /**
   * Equivalent to Shutdown(true), including after an earlier Shutdown(false).
   * Waits for an in-progress run unless called from the scheduler thread itself.
   */
  ~Scheduler();

  // Non-copyable: the scheduler owns its thread, so a copy would mean two owners of it.
  // Non-movable: a moved-from object would need null-state handling in every method;
  // pinning the object keeps that out. Hold it in a std::unique_ptr if it must move.
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  Scheduler(Scheduler&&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  /**
   * Schedules a task to run every `period`, first run one `period` from now. Runs are
   * fixed-rate; if a run overruns, missed runs are skipped rather than run in a burst.
   * The outcome of each run is reported to `callback`.
   *
   * Returns the ID used to cancel the task, or std::nullopt if the scheduler has been
   * shut down, the task or callback is empty, or the period is not positive or exceeds
   * MAX_PERIOD.
   */
  template <typename T>
  std::optional<TaskId> Schedule(
      Duration period,
      typename Callback<T>::Task task,
      std::shared_ptr<Callback<T>> callback
  ) {
    if (!task || !callback) {
      return std::nullopt;
    }
    return ScheduleRun(
        period, [task = std::move(task), callback = std::move(callback)] {
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
          // Callbacks run outside the try: an exception thrown by OnSuccess must not be
          // reported as a failure of the task
          if constexpr (std::is_void_v<T>) {
            try {
              task();
            } catch (...) {
              callback->OnFailure(std::current_exception());
              return;
            }
            callback->OnSuccess();
          } else {
            std::optional<T> result;
            try {
              result.emplace(task());
            } catch (...) {
              callback->OnFailure(std::current_exception());
              return;
            }
            callback->OnSuccess(std::move(*result));
          }
#else
          if constexpr (std::is_void_v<T>) {
            task();
            callback->OnSuccess();
          } else {
            callback->OnSuccess(task());
          }
#endif
        }
    );
  }

  /**
   * Cancels a scheduled task so it never runs again. A run already in progress is not
   * interrupted. Returns false if no such task is scheduled.
   */
  bool Cancel(TaskId id);

  /**
   * Stops the scheduler thread and drops all scheduled tasks. A run in progress is not
   * interrupted.
   *
   * @param wait If true, blocks until the in-progress run (if any) finishes and the
   *  thread exits. If false, returns immediately and the thread exits on its own.
   *  Destruction or a later Shutdown(true) still waits for that thread to exit.
   *  When called from a task on the scheduler thread, never blocks.
   */
  void Shutdown(bool wait);

 private:
  using Clock = std::chrono::steady_clock;

  struct Task {
    Clock::time_point next_run;
    Clock::duration period;
    // Shared so a run can proceed outside the lock even if the task is cancelled
    std::shared_ptr<std::function<void()>> fn;
  };

  // Shared with the thread so a detached thread (Shutdown(false)) never outlives it
  struct State {
    std::mutex mutex;
    std::condition_variable cv;
    bool stopped = false;
    bool exited = false;
    TaskId next_id = 1;
    std::unordered_map<TaskId, Task> tasks;
  };

  // Callback-less entry point; private so every task reports its outcome
  std::optional<TaskId> ScheduleRun(Duration period, std::function<void()> run);

  static void ThreadMain(const std::shared_ptr<State>& state);

  std::shared_ptr<State> _state;
  std::thread _thread;
  const std::thread::id _worker_id;
};

}  // namespace datadog::impl
