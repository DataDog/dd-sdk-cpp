// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>

#include "datadog/timestamp.hpp"

#include "datadog/impl/core/executor.hpp"
#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Receives the outcome of every run of a scheduled task. Both methods are called on the
 * scheduler's queue. An exception escaping either of them is logged and the task stays
 * scheduled; without exception support it terminates the program.
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
 * Runs periodic tasks on a serial queue of the SDK's executor.
 *
 * Each task's next run is a delayed task on the queue, so the scheduler owns no thread
 * and never busy-waits. Tasks run serially, one at a time, and use a monotonic clock
 * so that wall-clock changes don't affect their cadence. A run may start late if the
 * queue or the executor is busy; see Executor.
 *
 * Schedule(), Cancel() and Shutdown() are thread-safe. The destructor must not run
 * concurrently with a Shutdown() called from a thread other than one running a task.
 */
class Scheduler final {
 public:
  using TaskId = std::uint64_t;

  /** Longest accepted period; keeps deadline arithmetic far from clock overflow. */
  static constexpr Duration MAX_PERIOD = std::chrono::hours(24 * 365);

  /**
   * Creates a scheduler that runs its tasks on `queue`, which it takes ownership of.
   * `diagnostic_logger` reports exceptions from callbacks.
   */
  explicit Scheduler(
      std::unique_ptr<TaskQueue> queue, DiagnosticLogger diagnostic_logger = {}
  );

  /**
   * Equivalent to Shutdown(true), including after an earlier Shutdown(false). When run
   * from a task, which can't wait for itself, the run in progress finishes after the
   * scheduler is destroyed, without touching it.
   */
  ~Scheduler();

  // Non-copyable and non-movable: tasks in flight refer to the scheduler's state.
  // Hold it in a std::unique_ptr if it must move.
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  Scheduler(Scheduler&&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  /**
   * Schedules a task to run every `period`, first run one `period` from now. Runs are
   * fixed-rate: if a run overruns, the ticks it overran are skipped rather than run in
   * a burst, and the next run happens on the original grid. Tasks run serially, so
   * keep them short and offload long work to another queue.
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
   * Cancels a scheduled task so it is never dispatched again. Returns false if no such
   * task is scheduled.
   *
   * Does not wait for a run already in progress or being dispatched: it may still
   * execute once after Cancel() returns. The task's closure stays alive until then, so
   * capture state by value or std::shared_ptr rather than by reference.
   */
  bool Cancel(TaskId id);

  /**
   * Stops the scheduler and drops all scheduled tasks. A run in progress is not
   * interrupted.
   *
   * @param wait If true, blocks until the in-progress run (if any) finishes. If false,
   *  returns immediately; destruction or a later Shutdown(true) waits for it.
   *  When called from a task, never blocks.
   */
  void Shutdown(bool wait);

 private:
  using Clock = std::chrono::steady_clock;

  struct Task final {
    Clock::time_point next_run;
    Clock::duration period;
    // Shared so a run can proceed outside the lock even if the task is cancelled
    std::shared_ptr<std::function<void()>> fn;
    // The delayed task on the queue that triggers the next run
    std::optional<TaskQueue::TimerId> timer;
  };

  // Shared with in-flight runs, which may outlive the scheduler when it's destroyed
  // from a task
  struct State final {
    explicit State(DiagnosticLogger logger) : diagnostic_logger(std::move(logger)) {}

    std::mutex mutex;
    bool stopped = false;
    TaskId next_id = 1;
    std::unordered_map<TaskId, Task> tasks;
    // Valid while !stopped: the scheduler owns the queue, and sets `stopped` before
    // releasing it
    TaskQueue* queue = nullptr;
    const DiagnosticLogger diagnostic_logger;
  };

  // Callback-less entry point; private so every task reports its outcome
  std::optional<TaskId> ScheduleRun(Duration period, std::function<void()> run);

  // Arms the delayed task for `task`'s next run. Called with state->mutex held.
  static bool ArmLocked(const std::shared_ptr<State>& state, TaskId id, Task& task);

  // Runs on the queue when a task's delayed trigger fires
  static void RunTask(const std::shared_ptr<State>& state, TaskId id);

  std::shared_ptr<State> _state;
  std::unique_ptr<TaskQueue> _queue;
};

}  // namespace datadog::impl
