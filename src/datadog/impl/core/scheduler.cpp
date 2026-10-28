// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/scheduler.hpp"

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
  const auto next_run = Clock::now() + clock_period;
  _state->tasks.emplace(
      id,
      Task{
          next_run,
          clock_period,
          std::make_shared<std::function<void()>>(std::move(run))
      }
  );
  _state->deadlines.emplace(next_run, id);
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
    if (cancelled) {
      _state->deadlines.erase({cancelled.mapped().next_run, id});
    }
  }
  return static_cast<bool>(cancelled);
}

void Scheduler::Shutdown(bool wait) {
  // Swap the tasks out before taking the lock so their closures are destroyed once the
  // lock is gone: a destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task> tasks;
  // Exactly one caller takes the thread; concurrent callers wait for `exited` instead
  std::thread thread;
  {
    std::lock_guard lock(_state->mutex);
    _state->stopped = true;
    tasks.swap(_state->tasks);
    _state->deadlines.clear();
    thread = std::move(_thread);
    _state->cv.notify_all();
  }

  // Waiting from the scheduler thread itself would deadlock, even after detach
  const bool on_worker = _worker_id == std::this_thread::get_id();
  if (thread.joinable()) {
    if (wait && !on_worker) {
      // Outside the lock: a task calling Shutdown() meanwhile must not block on it
      thread.join();
    } else {
      thread.detach();
    }
  } else if (wait && !on_worker) {
    std::unique_lock lock(_state->mutex);
    _state->cv.wait(lock, [&] { return _state->exited; });
  }
}

void Scheduler::ThreadMain(const std::shared_ptr<State>& state) {
  std::unique_lock lock(state->mutex);
  while (!state->stopped) {
    // Sleep until something changes or the earliest task is due, then re-evaluate:
    // tasks may have been added or cancelled in the meantime
    if (state->deadlines.empty()) {
      state->cv.wait(lock);
      continue;
    }
    // Copied: wait_until reads the deadline after unlocking, when the entry may
    // already be erased by Cancel()/Shutdown()
    const auto [deadline, id] = *state->deadlines.begin();
    if (deadline > Clock::now()) {
      state->cv.wait_until(lock, deadline);
      continue;
    }

    auto fn = state->tasks.at(id).fn;
    lock.unlock();
    (*fn)();
    // Drop the closure before relocking: if the task was cancelled meanwhile, this copy
    // is the last reference, and its destructor must not run under the mutex
    fn.reset();
    lock.lock();

    // Fixed-rate: move to the first tick after the run ended, skipping the ticks it
    // overran. Looked up again: the task may have been cancelled during the run
    if (auto it = state->tasks.find(id); it != state->tasks.end()) {
      Task& done = it->second;
      state->deadlines.erase({done.next_run, id});
      const auto overdue = Clock::now() - done.next_run;
      done.next_run += (overdue / done.period + 1) * done.period;
      state->deadlines.emplace(done.next_run, id);
    }
  }
  state->exited = true;
  state->cv.notify_all();
}

}  // namespace datadog::impl
