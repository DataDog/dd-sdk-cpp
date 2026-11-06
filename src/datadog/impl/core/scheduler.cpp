// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/scheduler.hpp"

#include <algorithm>
#include <utility>

namespace datadog::impl {

Scheduler::Scheduler(DiagnosticLogger diagnostic_logger)
    : _state(std::make_shared<State>(std::move(diagnostic_logger))),
      _thread(ThreadMain, _state),
      _worker_id(_thread.get_id()) {}

Scheduler::~Scheduler() {
  Shutdown(true);
  // Destroyed from a task: the thread cannot join itself, so it exits on its own and
  // keeps the state alive through its own reference
  if (_thread.joinable()) {
    _thread.detach();
  }
}

std::optional<Scheduler::TaskId> Scheduler::ScheduleRun(
    Duration period, std::function<void()> run
) {
  // Checked before conversion: huge periods would overflow the clock arithmetic
  if (period > MAX_PERIOD) {
    return std::nullopt;
  }
  const auto clock_period = std::chrono::duration_cast<Clock::duration>(period);
  if (clock_period <= Clock::duration::zero() || !run) {
    return std::nullopt;
  }

  std::lock_guard lock(_state->mutex);
  if (_state->stopped) {
    return std::nullopt;
  }
  const TaskId id = _state->next_id++;
  const auto next_run = Clock::now() + clock_period;
  _state->tasks.emplace(
      id,
      Task{
          next_run,
          clock_period,
          std::make_shared<std::function<void()>>(std::move(run))
      }
  );
  // Wake the thread: the new task may be due earlier than the one it's waiting for
  _state->cv.notify_all();
  return id;
}

bool Scheduler::Cancel(TaskId id) {
  // No need to wake the thread: if it was waiting for this task, it wakes up at the
  // cancelled deadline, finds nothing due, and goes back to sleep
  // Declared before the lock so the extracted closure is destroyed after unlocking: a
  // destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task>::node_type cancelled;
  {
    std::lock_guard lock(_state->mutex);
    cancelled = _state->tasks.extract(id);
  }
  return static_cast<bool>(cancelled);
}

void Scheduler::Shutdown(bool wait) {
  // Declared before the lock so the swapped-out closures are destroyed after unlocking:
  // a destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task> tasks;
  // Never detached here: only join() waits for the thread to fully exit, thread_local
  // destructors included. One waiting caller takes the thread to join it; the others
  // wait for `joined`, or leave the thread to a later Shutdown(true) or the destructor
  std::thread thread;
  // Joining from the scheduler thread itself would deadlock
  const bool can_wait = wait && _worker_id != std::this_thread::get_id();
  {
    std::lock_guard lock(_state->mutex);
    _state->stopped = true;
    tasks.swap(_state->tasks);
    if (can_wait) {
      thread = std::move(_thread);
    }
    _state->cv.notify_all();
  }

  if (thread.joinable()) {
    // Outside the lock: a task calling Shutdown() meanwhile must not block on it
    thread.join();
    std::lock_guard lock(_state->mutex);
    _state->joined = true;
    _state->cv.notify_all();
  } else if (can_wait) {
    std::unique_lock lock(_state->mutex);
    _state->cv.wait(lock, [&] { return _state->joined; });
  }
}

void Scheduler::ThreadMain(const std::shared_ptr<State>& state) {
  std::unique_lock lock(state->mutex);
  while (!state->stopped) {
    // ponytail: O(n) scan per wakeup, fine for a handful of tasks; keep a
    // std::set<pair<time_point, TaskId>> of deadlines if many tasks get scheduled
    auto next = std::min_element(
        state->tasks.begin(), state->tasks.end(), [](const auto& a, const auto& b) {
          return a.second.next_run < b.second.next_run;
        }
    );

    // Sleep until something changes or the earliest task is due, then re-evaluate:
    // tasks may have been added or cancelled in the meantime
    if (next == state->tasks.end()) {
      state->cv.wait(lock);
      continue;
    }
    // Copied: wait_until reads the deadline after unlocking, when the task may
    // already be erased by Cancel()/Shutdown()
    const TaskId id = next->first;
    const auto deadline = next->second.next_run;
    if (deadline > Clock::now()) {
      state->cv.wait_until(lock, deadline);
      continue;
    }

    auto fn = next->second.fn;
    lock.unlock();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    // Task exceptions already go to OnFailure; this catches the callback's own, which
    // must not unwind out of the thread and terminate the application
    try {
      (*fn)();
    } catch (...) {
      state->diagnostic_logger.Error("Scheduler task callback threw an exception");
    }
#else
    (*fn)();
#endif
    // Drop the closure before relocking: if the task was cancelled meanwhile, this copy
    // is the last reference, and its destructor must not run under the mutex
    fn.reset();
    lock.lock();

    // Fixed-rate: move to the first tick after the run ended, skipping the ticks it
    // overran. Looked up again: the task may have been cancelled during the run
    if (auto it = state->tasks.find(id); it != state->tasks.end()) {
      Task& done = it->second;
      const auto overdue = Clock::now() - done.next_run;
      done.next_run += (overdue / done.period + 1) * done.period;
    }
  }
}

}  // namespace datadog::impl
