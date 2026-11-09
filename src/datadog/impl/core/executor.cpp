// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/executor.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "datadog/impl/types/assert.hpp"

#if defined(_WIN32)
#include "datadog/impl/types/windows_headers.hpp"
#elif defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

namespace datadog::impl {
namespace executor_detail {

using Clock = std::chrono::steady_clock;

// When other work is waiting for a worker, a queue gives up its worker after running
// this many items, or after this much time has passed, whichever comes first. Bounds
// how long one busy queue can delay the others when the thread limit is low.
static constexpr size_t YIELD_AFTER_ITEMS = 32;
static constexpr auto YIELD_AFTER_TIME = std::chrono::milliseconds(2);

// Sentinel for "no pending timer" in ExecutorState::next_deadline
static constexpr Clock::rep NO_DEADLINE = std::numeric_limits<Clock::rep>::max();

// The queue whose item is running on this thread, if any
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static thread_local const QueueCore* t_current_queue = nullptr;

// The executor that owns this thread, if it's a worker thread
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static thread_local const ExecutorState* t_worker_executor = nullptr;

// Names the calling thread so that SDK workers are identifiable in debuggers and
// profilers. Best-effort: does nothing on platforms without a supported API.
static void SetCurrentThreadName(size_t index) {
  // Linux limits thread names to 15 characters
  const std::string name = "dd-worker-" + std::to_string(index);
#if defined(__APPLE__)
  pthread_setname_np(name.c_str());
#elif defined(__linux__)
  pthread_setname_np(pthread_self(), name.c_str());
#elif defined(_WIN32)
  // SetThreadDescription is only available on Windows 10 1607 and later, so it's
  // looked up at runtime rather than linked
  using SetThreadDescriptionFunc = HRESULT(WINAPI*)(HANDLE, PCWSTR);
  HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
  if (kernel32 != nullptr) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto set_description = reinterpret_cast<SetThreadDescriptionFunc>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "SetThreadDescription"))
    );
    if (set_description != nullptr) {
      const std::wstring wide_name(name.begin(), name.end());
      set_description(GetCurrentThread(), wide_name.c_str());
    }
  }
#else
  (void)name;
#endif
}

/**
 * A delayed task, waiting to be pushed onto its queue.
 */
struct TimerEntry {
  std::weak_ptr<QueueCore> queue;
  const QueueCore* owner{nullptr};
  std::function<void()> task;
};

/**
 * A worker thread, along with the state used to wake it individually: each idle worker
 * waits on its own condition variable, so that waking work up claims exactly one
 * specific worker.
 */
struct Worker {
  size_t index{0};
  std::thread thread;
  std::condition_variable cv;
  bool woken{false};  // Guarded by ExecutorState::mutex
};

/**
 * State shared by an Executor, its queues, and its worker threads.
 */
struct ExecutorState {
  ExecutorState(uint32_t in_max_worker_threads, DiagnosticLogger in_diagnostic_logger)
      : max_worker_threads(in_max_worker_threads),
        diagnostic_logger(std::move(in_diagnostic_logger)) {}

  const uint32_t max_worker_threads;
  const DiagnosticLogger diagnostic_logger;

  mutable std::mutex mutex;
  bool stopping{false};
  bool joined{false};

  // Every queue created from this executor that still exists, for sizing and for
  // CloseDiscardableQueues()
  std::vector<std::weak_ptr<QueueCore>> queues;
  size_t live_queue_count{0};

  // Queues that have work and are waiting for a worker, in FIFO order. Each queue
  // appears at most once: a queue is only added when it moves to the Scheduled state.
  std::deque<std::shared_ptr<QueueCore>> runnable;

  std::vector<std::unique_ptr<Worker>> workers;
  size_t num_workers_spawned{0};
  std::vector<Worker*> idle_workers;

  // Workers that have been woken or spawned for new work but haven't yet looked for
  // it. Each runnable queue beyond this count still needs a worker.
  size_t pending_wakeups{0};

  // Bumped whenever a new timer becomes the earliest, so that every idle worker
  // recomputes the deadline it sleeps until. Otherwise, if the one worker sleeping on
  // the earliest deadline were claimed for other work, the others would sleep through
  // it.
  uint64_t timer_generation{0};

  // Set when spawning a worker fails, so that the failure is logged once the executor's
  // mutex is released: the logger may call back into the SDK
  std::atomic<bool> spawn_failed{false};

  // Delayed tasks, ordered by deadline. Cancelled entries are removed from `timers`
  // immediately and from `timer_heap` lazily.
  using HeapEntry = std::pair<Clock::time_point, TaskQueueCore::TimerId>;
  std::priority_queue<HeapEntry, std::vector<HeapEntry>, std::greater<>> timer_heap;
  std::unordered_map<TaskQueueCore::TimerId, TimerEntry> timers;
  TaskQueueCore::TimerId next_timer_id{1};

  // Mirrors of the state above that queues read without locking, after every item, to
  // decide whether to yield. Relaxed loads suffice: a stale value only delays a yield
  // by one item, or causes one unnecessary yield.
  std::atomic<size_t> runnable_count{0};
  std::atomic<size_t> idle_worker_count{0};
  std::atomic<Clock::rep> next_deadline{NO_DEADLINE};

  size_t GetWorkerLimitLocked() const {
    if (max_worker_threads == 0) {
      return std::max<size_t>(1, live_queue_count);
    }
    return max_worker_threads;
  }

  void SyncRunnableCountLocked() {
    runnable_count.store(runnable.size(), std::memory_order_relaxed);
  }

  void SyncIdleWorkerCountLocked() {
    idle_worker_count.store(idle_workers.size(), std::memory_order_relaxed);
  }

  // Drops cancelled entries from the top of the heap, so that the top is always the
  // earliest live timer, and updates next_deadline to match
  void PruneTimerHeapLocked() {
    while (!timer_heap.empty() &&
           timers.find(timer_heap.top().second) == timers.end()) {
      timer_heap.pop();
    }
    next_deadline.store(
        timer_heap.empty() ? NO_DEADLINE
                           : timer_heap.top().first.time_since_epoch().count(),
        std::memory_order_relaxed
    );
  }

  /**
   * Returns true if another queue is waiting for a worker.
   */
  bool HasRunnableQueues() const {
    return runnable_count.load(std::memory_order_relaxed) > 0;
  }

  /**
   * Returns true if a timer is pending and no idle worker is around to fire it, so a
   * busy worker may need to yield in order to fire it.
   */
  bool HasTimerWithoutIdleWorker() const {
    return idle_worker_count.load(std::memory_order_relaxed) == 0 &&
           next_deadline.load(std::memory_order_relaxed) != NO_DEADLINE;
  }

  bool IsTimerDue(Clock::time_point now) const {
    return next_deadline.load(std::memory_order_relaxed) <=
           now.time_since_epoch().count();
  }

  // Wakes one idle worker, or spawns a new one if none is idle and the limit allows
  void WakeOrSpawnWorkerLocked() {
    if (stopping) {
      return;
    }
    if (!idle_workers.empty()) {
      Worker* worker = idle_workers.back();
      idle_workers.pop_back();
      SyncIdleWorkerCountLocked();
      worker->woken = true;
      pending_wakeups++;
      worker->cv.notify_one();
      return;
    }
    if (workers.size() < GetWorkerLimitLocked() && SpawnWorkerLocked()) {
      pending_wakeups++;
    }
  }

  // Wakes or spawns a worker for each runnable queue that no worker is on its way to
  void EnsureWorkersForRunnableLocked() {
    if (runnable.size() > pending_wakeups) {
      WakeOrSpawnWorkerLocked();
    }
  }

  bool SpawnWorkerLocked() {
    auto worker = std::make_unique<Worker>();
    worker->index = num_workers_spawned + 1;
    Worker* raw_worker = worker.get();
#if defined(__cpp_exceptions) || defined(_CPPUNWIND)
    try {
      worker->thread = std::thread([this, raw_worker] { WorkerMain(*raw_worker); });
    } catch (...) {
      // Queued work still runs on the workers that already exist, if any; see
      // QueueCore::WaitIdle() for the case where none does
      spawn_failed.store(true, std::memory_order_relaxed);
      return false;
    }
#else
    worker->thread = std::thread([this, raw_worker] { WorkerMain(*raw_worker); });
#endif
    num_workers_spawned++;
    workers.push_back(std::move(worker));
    return true;
  }

  // Logs a spawn failure recorded under the lock. Must be called without holding it.
  void LogSpawnFailure() {
    // Checked with a plain load first: this runs on every hand-off to a worker, and an
    // unconditional read-modify-write would contend on the flag's cache line
    if (spawn_failed.load(std::memory_order_relaxed) &&
        spawn_failed.exchange(false, std::memory_order_relaxed)) {
      diagnostic_logger.Error("Failed to spawn SDK worker thread");
    }
  }

  // Called by WaitIdle() before waiting for a queue. If no worker exists because
  // spawning has failed so far, tries again. Returns false if no worker can run it.
  bool EnsureAnyWorkerLocked() {
    if (!workers.empty()) {
      return true;
    }
    if (!stopping && SpawnWorkerLocked()) {
      pending_wakeups++;
      return true;
    }
    return false;
  }

  void MakeRunnable(std::shared_ptr<QueueCore> queue) {
    {
      std::lock_guard lock(mutex);
      DATADOG_ASSERT(!joined, "Work submitted to an Executor after Shutdown()");
      runnable.push_back(std::move(queue));
      SyncRunnableCountLocked();
      EnsureWorkersForRunnableLocked();
    }
    LogSpawnFailure();
  }

  std::optional<TaskQueueCore::TimerId> AddTimerLocked(
      const std::shared_ptr<QueueCore>& queue,
      Duration delay,
      std::function<void()> task
  ) {
    if (stopping) {
      return std::nullopt;
    }
    const TaskQueueCore::TimerId id = next_timer_id++;
    const Clock::time_point deadline =
        Clock::now() +
        std::chrono::duration_cast<Clock::duration>(std::max(delay, Duration::zero()));

    // The heap's top is always a live timer (see PruneTimerHeapLocked)
    const bool is_new_earliest =
        timer_heap.empty() || deadline < timer_heap.top().first;
    timer_heap.emplace(deadline, id);
    timers.emplace(id, TimerEntry{queue, queue.get(), std::move(task)});
    PruneTimerHeapLocked();

    // Idle workers sleep until the earliest deadline they knew of: if this timer is now
    // the earliest, have all of them recompute it, and make sure some worker exists to
    // fire it
    if (is_new_earliest) {
      timer_generation++;
      for (Worker* worker : idle_workers) {
        worker->cv.notify_one();
      }
      if (idle_workers.empty() && !stopping &&
          workers.size() < GetWorkerLimitLocked() && SpawnWorkerLocked()) {
        pending_wakeups++;
      }
    }
    return id;
  }

  // Removes timers matching `pred`, returning them so they can be destroyed without
  // holding the lock
  template <typename Predicate>
  std::vector<TimerEntry> TakeTimersLocked(Predicate pred) {
    std::vector<TimerEntry> taken;
    for (auto it = timers.begin(); it != timers.end();) {
      if (pred(it->first, it->second)) {
        taken.push_back(std::move(it->second));
        it = timers.erase(it);
      } else {
        ++it;
      }
    }
    PruneTimerHeapLocked();
    return taken;
  }

  // Pushes every due timer's task onto its queue. Called with `lock` held; releases it
  // while pushing, since pushing acquires queue locks
  void FireDueTimersLocked(std::unique_lock<std::mutex>& lock) {
    if (timer_heap.empty()) {
      return;
    }
    std::vector<TimerEntry> due;
    const auto now = Clock::now();
    while (!timer_heap.empty() && timer_heap.top().first <= now) {
      const auto id = timer_heap.top().second;
      timer_heap.pop();
      if (auto it = timers.find(id); it != timers.end()) {
        due.push_back(std::move(it->second));
        timers.erase(it);
      }
    }
    if (due.empty()) {
      return;
    }
    PruneTimerHeapLocked();

    lock.unlock();
    for (auto& entry : due) {
      if (auto queue = entry.queue.lock()) {
        // If the queue was closed in the meantime, the push is rejected and the task is
        // destroyed below
        queue->PushDelayedTask(std::move(entry.task));
      }
    }
    due.clear();
    lock.lock();
  }

  void WorkerMain(Worker& self) {
    SetCurrentThreadName(self.index);
    t_worker_executor = this;

    std::unique_lock lock(mutex);
    // A new worker was counted as a pending wakeup when it was spawned
    bool is_pending_wakeup = true;
    while (true) {
      FireDueTimersLocked(lock);

      // This worker is about to take work, or go idle: it's no longer on its way
      if (is_pending_wakeup) {
        pending_wakeups--;
        is_pending_wakeup = false;
      }

      if (!runnable.empty()) {
        std::shared_ptr<QueueCore> queue = std::move(runnable.front());
        runnable.pop_front();
        SyncRunnableCountLocked();

        // The worker that was woken for this queue may find another one instead: make
        // sure every remaining queue still gets a worker
        EnsureWorkersForRunnableLocked();

        lock.unlock();
        LogSpawnFailure();
        const bool yielded = queue->Run();
        if (yielded) {
          // The queue still has work: fire any due timers first, so that the work they
          // push gets ahead of the queue that yielded, then put it back in line
          lock.lock();
          FireDueTimersLocked(lock);
          runnable.push_back(std::move(queue));
          SyncRunnableCountLocked();
          continue;
        }
        // Released before relocking: this may be the last reference to the queue, and
        // its destructor acquires the executor's mutex
        queue.reset();
        lock.lock();
        continue;
      }

      if (stopping) {
        break;
      }

      // Nothing to do: wait to be claimed for new work, or until the next timer is due
      self.woken = false;
      idle_workers.push_back(&self);
      SyncIdleWorkerCountLocked();
      const uint64_t generation = timer_generation;
      auto should_wake = [&] {
        return self.woken || stopping || timer_generation != generation;
      };
      if (timer_heap.empty()) {
        self.cv.wait(lock, should_wake);
      } else {
        // Copied: wait_until reads the deadline after releasing the lock, when the heap
        // may have been modified
        const Clock::time_point deadline = timer_heap.top().first;
        self.cv.wait_until(lock, deadline, should_wake);
      }
      if (self.woken) {
        // Claimed by WakeOrSpawnWorkerLocked() or Shutdown(), which counted it
        is_pending_wakeup = true;
      } else {
        // Woken by a timeout or a new earliest timer: no longer idle. If there's still
        // nothing to do, it rejoins the idle workers with an up-to-date deadline.
        idle_workers.erase(
            std::remove(idle_workers.begin(), idle_workers.end(), &self),
            idle_workers.end()
        );
        SyncIdleWorkerCountLocked();
      }
    }

    t_worker_executor = nullptr;
  }

  void Register(const std::shared_ptr<QueueCore>& queue) {
    std::lock_guard lock(mutex);
    queues.push_back(queue);
    live_queue_count++;
  }

  void Unregister() {
    std::lock_guard lock(mutex);
    live_queue_count--;
    queues.erase(
        std::remove_if(
            queues.begin(),
            queues.end(),
            [](const std::weak_ptr<QueueCore>& q) { return q.expired(); }
        ),
        queues.end()
    );
  }
};

QueueCore::QueueCore(
    std::shared_ptr<ExecutorState> executor, const QueueOptions& options
)
    : _executor(std::move(executor)),
      _options(options),
      _suspended(options.start_suspended) {}

QueueCore::~QueueCore() { _executor->Unregister(); }

bool QueueCore::OnItemAddedLocked() {
  if (_state == State::Idle && !_suspended) {
    _state = State::Scheduled;
    return true;
  }
  return false;
}

void QueueCore::MakeRunnable() { _executor->MakeRunnable(shared_from_this()); }

bool QueueCore::Run() {
  std::unique_lock lock(_mutex);
  DATADOG_ASSERT(_state == State::Scheduled, "QueueCore::Run on a queue not scheduled");
  _state = State::Running;

  const QueueCore* const previous_queue = t_current_queue;
  t_current_queue = this;

  // The yield budget counts from when this worker picked the queue up, so that an item
  // that blocked for a long time is followed by a yield if other work started waiting
  // meanwhile
  const auto run_started = Clock::now();
  size_t items_run = 0;
  bool yielded = false;

  while (RunNextLocked(lock)) {
    items_run++;
    if (!HasItemsLocked()) {
      continue;  // Loop exits on the next check
    }

    // Fast path: when no other work is waiting, keep going without reading the clock
    const bool queues_waiting = _executor->HasRunnableQueues();
    if (!queues_waiting && !_executor->HasTimerWithoutIdleWorker()) {
      continue;
    }

    const auto now = Clock::now();
    if (_executor->IsTimerDue(now) ||
        (queues_waiting &&
         (items_run >= YIELD_AFTER_ITEMS || now - run_started >= YIELD_AFTER_TIME))) {
      yielded = true;
      break;
    }
  }

  t_current_queue = previous_queue;

  if (yielded) {
    // Stays scheduled: the worker puts it back on the runnable list
    _state = State::Scheduled;
    return true;
  }

  _state = State::Idle;
  _idle_cv.notify_all();
  return false;
}

void QueueCore::Close() {
  std::unique_lock lock(_mutex);
  _closed = true;

  // Cancel this queue's delayed tasks under the queue lock, so that none can be added
  // after the queue is closed
  std::vector<TimerEntry> cancelled;
  {
    std::lock_guard executor_lock(_executor->mutex);
    cancelled = _executor->TakeTimersLocked([this](auto, const TimerEntry& entry) {
      return entry.owner == this;
    });
  }

  if (_options.stop_policy == StopPolicy::DiscardPending) {
    DiscardLocked(lock);
  }

  // A suspended queue must still drain when closed, or WaitIdle() would never return
  bool make_runnable = false;
  if (_suspended) {
    _suspended = false;
    if (_state == State::Idle && HasItemsLocked()) {
      _state = State::Scheduled;
      make_runnable = true;
    }
  }

  lock.unlock();
  cancelled.clear();
  if (make_runnable) {
    MakeRunnable();
  }
}

void QueueCore::Abandon() {
  std::unique_lock lock(_mutex);
  _closed = true;
  std::vector<TimerEntry> cancelled;
  {
    std::lock_guard executor_lock(_executor->mutex);
    cancelled = _executor->TakeTimersLocked([this](auto, const TimerEntry& entry) {
      return entry.owner == this;
    });
  }
  DiscardLocked(lock);
  lock.unlock();
  cancelled.clear();
}

void QueueCore::WaitIdle() {
  // Waiting on our own queue from one of its items would never return
  DATADOG_ASSERT(!IsCurrent(), "WaitIdle() called from the queue's own item");
  if (IsCurrent()) {
    return;
  }
  bool can_run = true;
  {
    // Once the workers are joined, nothing would ever make the queue idle. If spawning
    // workers has failed so far, try again: otherwise nothing would run the queue.
    std::lock_guard executor_lock(_executor->mutex);
    can_run = !_executor->joined && _executor->EnsureAnyWorkerLocked();
  }
  _executor->LogSpawnFailure();
  if (!can_run) {
    _executor->diagnostic_logger.Error(
        "SDK background work dropped: no worker thread available",
        {{"queue", GetName()}}
    );
    return;
  }
  std::unique_lock lock(_mutex);
  _idle_cv.wait(lock, [this] {
    return _state == State::Idle && (_suspended || !HasItemsLocked());
  });
}

void QueueCore::Resume() {
  bool make_runnable = false;
  {
    std::lock_guard lock(_mutex);
    if (!_suspended) {
      return;
    }
    _suspended = false;
    if (_state == State::Idle && HasItemsLocked()) {
      _state = State::Scheduled;
      make_runnable = true;
    }
  }
  if (make_runnable) {
    MakeRunnable();
  }
}

bool QueueCore::IsCurrent() const { return t_current_queue == this; }

void QueueCore::ReleaseFromHandle(QueueCore& core) {
  if (core.IsCurrent()) {
    core.Abandon();
  } else {
    core.Close();
    core.WaitIdle();
  }
}

TaskQueueCore::TaskQueueCore(
    std::shared_ptr<ExecutorState> executor, const QueueOptions& options
)
    : QueueCore(std::move(executor), options) {}

bool TaskQueueCore::Push(std::function<void()>&& task) {
  if (!task) {
    return false;
  }
  bool make_runnable = false;
  {
    std::lock_guard lock(GetMutex());
    if (IsClosedLocked()) {
      return false;
    }
    _items.emplace_back(std::move(task));
    make_runnable = OnItemAddedLocked();
  }
  if (make_runnable) {
    MakeRunnable();
  }
  return true;
}

std::optional<TaskQueueCore::TimerId> TaskQueueCore::PostAfter(
    Duration delay, std::function<void()> task
) {
  if (!task) {
    return std::nullopt;
  }
  ExecutorState& executor = GetExecutor();
  std::optional<TimerId> id;
  {
    std::lock_guard lock(GetMutex());
    if (IsClosedLocked()) {
      return std::nullopt;
    }
    std::lock_guard executor_lock(executor.mutex);
    id = executor.AddTimerLocked(shared_from_this(), delay, std::move(task));
  }
  executor.LogSpawnFailure();
  return id;
}

bool TaskQueueCore::CancelTimer(TimerId id) {
  std::vector<TimerEntry> cancelled;
  {
    ExecutorState& executor = GetExecutor();
    std::lock_guard executor_lock(executor.mutex);
    cancelled =
        executor.TakeTimersLocked([this, id](
                                      TimerId entry_id, const TimerEntry& entry
                                  ) { return entry_id == id && entry.owner == this; });
  }
  return !cancelled.empty();
}

bool TaskQueueCore::RunNextLocked(std::unique_lock<std::mutex>& lock) {
  if (_items.empty()) {
    return false;
  }
  {
    // Destroyed before the lock is reacquired, so that its destructor may call back
    // into this queue
    std::function<void()> task = std::move(_items.front());
    _items.pop_front();
    lock.unlock();
    task();
  }
  lock.lock();
  return true;
}

void TaskQueueCore::DiscardLocked(std::unique_lock<std::mutex>& lock) {
  std::deque<std::function<void()>> discarded;
  discarded.swap(_items);
  lock.unlock();
  discarded.clear();
  lock.lock();
}

}  // namespace executor_detail

// Closing only locks, swaps containers and wakes threads. The analyzer can't rule out
// an escape through the inlined std::function machinery the queue code uses elsewhere;
// as with any destructor, an exception here would terminate.
// NOLINTNEXTLINE(bugprone-exception-escape)
TaskQueue::~TaskQueue() { executor_detail::QueueCore::ReleaseFromHandle(*_core); }

Executor::Executor(uint32_t max_worker_threads, DiagnosticLogger diagnostic_logger)
    : _state(
          std::make_shared<executor_detail::ExecutorState>(
              max_worker_threads, std::move(diagnostic_logger)
          )
      ) {}

Executor::~Executor() { Shutdown(); }

std::unique_ptr<TaskQueue> Executor::CreateTaskQueue(const QueueOptions& options) {
  auto core = std::make_shared<executor_detail::TaskQueueCore>(_state, options);
  Register(core);
  return std::make_unique<TaskQueue>(std::move(core));
}

void Executor::Register(const std::shared_ptr<executor_detail::QueueCore>& core) {
  _state->Register(core);
}

void Executor::CloseDiscardableQueues() {
  std::vector<std::shared_ptr<executor_detail::QueueCore>> discardable;
  {
    std::lock_guard lock(_state->mutex);
    for (const auto& weak_queue : _state->queues) {
      auto queue = weak_queue.lock();
      if (queue && queue->GetStopPolicy() == StopPolicy::DiscardPending) {
        discardable.push_back(std::move(queue));
      }
    }
  }
  for (const auto& queue : discardable) {
    queue->Close();
  }
  // Released without holding the executor's mutex: a queue destructor acquires it
  discardable.clear();
}

void Executor::Shutdown() {
  DATADOG_ASSERT(
      executor_detail::t_worker_executor != _state.get(),
      "Executor::Shutdown() called from one of its own worker threads"
  );

  std::vector<std::unique_ptr<executor_detail::Worker>> workers;
  std::unordered_map<TaskQueue::TimerId, executor_detail::TimerEntry> timers;
  {
    std::lock_guard lock(_state->mutex);
    if (_state->stopping) {
      return;
    }
    _state->stopping = true;
    timers.swap(_state->timers);
    _state->PruneTimerHeapLocked();
    for (executor_detail::Worker* worker : _state->idle_workers) {
      worker->woken = true;
      _state->pending_wakeups++;
      worker->cv.notify_one();
    }
    _state->idle_workers.clear();
    _state->SyncIdleWorkerCountLocked();
    workers.swap(_state->workers);
  }

  // Destroyed outside the lock, as task destructors may call back into the executor
  timers.clear();

  // Workers finish any runnable work, then exit
  for (auto& worker : workers) {
    if (worker->thread.joinable()) {
      worker->thread.join();
    }
  }

  std::lock_guard lock(_state->mutex);
  _state->joined = true;
}

size_t Executor::GetWorkerCount() const {
  std::lock_guard lock(_state->mutex);
  return _state->num_workers_spawned;
}

bool Executor::IsWorkerThread() {
  return executor_detail::t_worker_executor != nullptr;
}

}  // namespace datadog::impl
