// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/scheduler.hpp"

#include <algorithm>
#include <utility>

namespace datadog::impl {

Scheduler::Scheduler()
    : _state(std::make_shared<State>()),
      _thread(ThreadMain, _state),
      _worker_id(_thread.get_id()) {}

Scheduler::~Scheduler() { Shutdown(true); }

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
  _state->tasks.emplace(
      id,
      Task{
          Clock::now() + clock_period,
          clock_period,
          std::make_shared<std::function<void()>>(std::move(run))
      }
  );
  // Wake the thread: the new task may be due earlier than the one it's waiting for
  _state->cv.notify_all();
  return id;
}

bool Scheduler::Cancel(TaskId id) {
  // No need to wake the thread: if it was waiting for this task, it wakes up early,
  // finds nothing due, and goes back to sleep
  // Extract the task before taking the lock so its closure is destroyed once the lock
  // is gone: a destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task>::node_type cancelled;
  {
    std::lock_guard lock(_state->mutex);
    cancelled = _state->tasks.extract(id);
  }
  return static_cast<bool>(cancelled);
}

void Scheduler::Shutdown(bool wait) {
  // Swap the tasks out before taking the lock so their closures are destroyed once the
  // lock is gone: a destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task> tasks;
  {
    std::lock_guard lock(_state->mutex);
    _state->stopped = true;
    tasks.swap(_state->tasks);
    _state->cv.notify_all();
  }

  // Waiting from the scheduler thread itself would deadlock, even after detach
  if (_worker_id == std::this_thread::get_id()) {
    if (_thread.joinable()) {
      _thread.detach();
    }
    return;
  }
  if (wait) {
    if (_thread.joinable()) {
      _thread.join();
    } else {
      std::unique_lock lock(_state->mutex);
      _state->cv.wait(lock, [&] { return _state->exited; });
    }
  } else if (_thread.joinable()) {
    _thread.detach();
  }
}

void Scheduler::ThreadMain(const std::shared_ptr<State>& state) {
  std::unique_lock lock(state->mutex);
  while (!state->stopped) {
    // ponytail: O(n) scan per wakeup, fine for a handful of tasks; switch to a heap
    // if many tasks get scheduled
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
    const auto now = Clock::now();
    Task& task = next->second;
    if (task.next_run > now) {
      // Copy the deadline: wait_until reads it after unlocking, when the task may
      // already be erased by Cancel()/Shutdown()
      const auto deadline = task.next_run;
      state->cv.wait_until(lock, deadline);
      continue;
    }

    // Fixed-rate: keep the cadence, but skip runs we're already too late for
    task.next_run += task.period;
    if (task.next_run <= now) {
      task.next_run = now + task.period;
    }

    auto fn = task.fn;
    lock.unlock();
    (*fn)();
    // Drop the closure before relocking: if the task was cancelled meanwhile, this copy
    // is the last reference, and its destructor must not run under the mutex
    fn.reset();
    lock.lock();
  }
  state->exited = true;
  state->cv.notify_all();
}

}  // namespace datadog::impl
