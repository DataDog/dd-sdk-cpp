// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/scheduler.hpp"

#include <algorithm>
#include <utility>

namespace datadog::impl {

Scheduler::Scheduler(
    std::unique_ptr<TaskQueue> queue, DiagnosticLogger diagnostic_logger
)
    : _state(std::make_shared<State>(std::move(diagnostic_logger))),
      _queue(std::move(queue)) {
  _state->queue = _queue.get();
}

Scheduler::~Scheduler() {
  Shutdown(true);
  // Destroying the queue waits for it to be idle, or, when called from a task, drops
  // everything that hasn't started: the run in progress keeps the state alive through
  // its own reference, and sees `stopped`
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
  auto [it, inserted] = _state->tasks.emplace(
      id,
      Task{
          Clock::now() + clock_period,
          clock_period,
          std::make_shared<std::function<void()>>(std::move(run)),
          std::nullopt
      }
  );
  if (!ArmLocked(_state, id, it->second)) {
    // The queue is closed, e.g. by the executor shutting down discardable work
    _state->tasks.erase(it);
    return std::nullopt;
  }
  return id;
}

bool Scheduler::ArmLocked(const std::shared_ptr<State>& state, TaskId id, Task& task) {
  const auto delay = std::max(task.next_run - Clock::now(), Clock::duration::zero());
  // The trigger holds a weak reference: once the scheduler is gone, nothing should keep
  // its state alive
  task.timer = state->queue->PostAfter(
      std::chrono::duration_cast<Duration>(delay),
      [weak_state = std::weak_ptr<State>(state), id] {
        if (auto locked_state = weak_state.lock()) {
          RunTask(locked_state, id);
        }
      }
  );
  return task.timer.has_value();
}

void Scheduler::RunTask(const std::shared_ptr<State>& state, TaskId id) {
  std::shared_ptr<std::function<void()>> fn;
  {
    std::lock_guard lock(state->mutex);
    if (state->stopped) {
      return;
    }
    auto it = state->tasks.find(id);
    if (it == state->tasks.end()) {
      return;
    }
    it->second.timer.reset();
    fn = it->second.fn;
  }

#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
  // Task exceptions already go to OnFailure; this catches the callback's own, which
  // must not unwind out of the worker thread and terminate the application
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

  std::lock_guard lock(state->mutex);
  if (state->stopped) {
    return;
  }
  // Looked up again: the task may have been cancelled during the run
  auto it = state->tasks.find(id);
  if (it == state->tasks.end()) {
    return;
  }
  // Fixed-rate: move to the first tick after the run ended, skipping the ticks it
  // overran
  Task& task = it->second;
  const auto overdue = Clock::now() - task.next_run;
  if (overdue >= Clock::duration::zero()) {
    task.next_run += (overdue / task.period + 1) * task.period;
  }
  if (!ArmLocked(state, id, task)) {
    state->tasks.erase(it);
  }
}

bool Scheduler::Cancel(TaskId id) {
  // Declared before the lock so the extracted closure is destroyed after unlocking: a
  // destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task>::node_type cancelled;
  {
    std::lock_guard lock(_state->mutex);
    cancelled = _state->tasks.extract(id);
    if (cancelled && !_state->stopped) {
      // Destroys only the trigger, which holds no user state
      if (const auto timer = cancelled.mapped().timer) {
        _state->queue->CancelTimer(*timer);
      }
    }
  }
  return static_cast<bool>(cancelled);
}

void Scheduler::Shutdown(bool wait) {
  // Declared before the lock so the swapped-out closures are destroyed after unlocking:
  // a destructor calling back into the Scheduler would relock the mutex
  std::unordered_map<TaskId, Task> tasks;
  {
    std::lock_guard lock(_state->mutex);
    _state->stopped = true;
    tasks.swap(_state->tasks);
  }

  // Cancels every pending trigger and drops anything queued but not started
  _queue->Close();
  tasks.clear();

  // A task can't wait for its own run to finish
  if (wait && !_queue->IsCurrent()) {
    _queue->WaitIdle();
  }
}

}  // namespace datadog::impl
