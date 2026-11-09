// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include "datadog/timestamp.hpp"

#include "datadog/impl/types/diagnostics.hpp"

namespace datadog::impl {

/**
 * Determines what happens to a queue's backlog when the queue is closed.
 */
enum class StopPolicy : uint8_t {
  /**
   * Every item accepted before Close() still runs. Used for work whose loss would lose
   * data, such as event writes and context updates.
   */
  Drain,
  /**
   * Items that haven't started yet, and the queue's pending timers, are dropped on
   * Close(). Only an item that is already running is waited for. Used for work that is
   * safe to skip, such as upload cycles (unsent batches stay on disk) and periodic
   * sampling.
   */
  DiscardPending,
};

/**
 * Creation-time settings for a serial queue.
 */
struct QueueOptions {
  /**
   * Static string identifying the queue in diagnostics.
   */
  const char* name{""};
  StopPolicy stop_policy{StopPolicy::Drain};
  /**
   * If true, items pushed before Resume() is called are held without running. Lets the
   * owner accept work before its consumers are ready to receive it.
   */
  bool start_suspended{false};
};

namespace executor_detail {

struct ExecutorState;
class TaskQueueCore;

/**
 * Executor-facing state of one serial queue: the scheduling state machine and the
 * synchronization shared by all item types. Subclasses own the items.
 *
 * Lock order: a queue's mutex may be held while acquiring the executor's mutex, never
 * the reverse.
 */
class QueueCore : public std::enable_shared_from_this<QueueCore> {
 public:
  QueueCore(std::shared_ptr<ExecutorState> executor, const QueueOptions& options);
  virtual ~QueueCore();

  QueueCore(const QueueCore&) = delete;
  QueueCore& operator=(const QueueCore&) = delete;
  QueueCore(QueueCore&&) = delete;
  QueueCore& operator=(QueueCore&&) = delete;

  /**
   * Called by a worker thread to run this queue's items, until the queue is empty or
   * it yields to other work. Returns true if it yielded, in which case the queue is
   * still scheduled and the caller must put it back on the runnable list.
   */
  bool Run();

  void Close();
  void WaitIdle();
  void Resume();

  /**
   * Called when the owning handle is destroyed from one of this queue's own items:
   * that item is still running, so instead of waiting, drop everything that hasn't
   * started, regardless of stop policy.
   */
  void Abandon();

  bool IsCurrent() const;
  StopPolicy GetStopPolicy() const { return _options.stop_policy; }

  /**
   * Implements the destructor of the queue's handle: closes the queue and waits until
   * it's idle, or abandons it when called from one of its own items.
   */
  static void ReleaseFromHandle(QueueCore& core);

  /**
   * Called by the executor when a delayed task posted to this queue is due. Only task
   * queues have delayed tasks.
   */
  virtual bool PushDelayedTask(std::function<void()>&& /*task*/) { return false; }
  const char* GetName() const { return _options.name; }

 protected:
  /**
   * Called with `lock` held. If an item is queued, pops it, runs it with `lock`
   * released, and returns true with `lock` reacquired. Returns false if the queue is
   * empty.
   */
  virtual bool RunNextLocked(std::unique_lock<std::mutex>& lock) = 0;

  /**
   * Called with `lock` held. Moves every queued item out of the queue and destroys
   * them with `lock` released, so that item destructors may call back into the queue.
   */
  virtual void DiscardLocked(std::unique_lock<std::mutex>& lock) = 0;

  virtual bool HasItemsLocked() const = 0;

  /**
   * Called after an item has been added, with `lock` held. Returns whether the caller
   * must hand the queue to the executor, once it has released `lock`.
   */
  bool OnItemAddedLocked();

  /**
   * Hands this queue to the executor's runnable list. Must be called without holding
   * this queue's mutex.
   */
  void MakeRunnable();

  std::mutex& GetMutex() const { return _mutex; }
  bool IsClosedLocked() const { return _closed; }
  ExecutorState& GetExecutor() const { return *_executor; }

 private:
  enum class State : uint8_t {
    Idle,       // Not on the runnable list and not running
    Scheduled,  // On the runnable list, waiting for a worker
    Running,    // Being run by exactly one worker
  };

  std::shared_ptr<ExecutorState> _executor;
  const QueueOptions _options;

  mutable std::mutex _mutex;
  std::condition_variable _idle_cv;
  State _state{State::Idle};
  bool _closed{false};
  bool _suspended{false};
};

template <typename T>
class TypedQueueCore final : public QueueCore {
 public:
  TypedQueueCore(
      std::shared_ptr<ExecutorState> executor,
      const QueueOptions& options,
      std::function<void(T&)> handler
  )
      : QueueCore(std::move(executor), options), _handler(std::move(handler)) {}

  bool Push(T&& item) {
    bool make_runnable = false;
    {
      std::lock_guard lock(GetMutex());
      if (IsClosedLocked()) {
        return false;
      }
      _items.emplace_back(std::move(item));
      make_runnable = OnItemAddedLocked();
    }
    if (make_runnable) {
      MakeRunnable();
    }
    return true;
  }

 protected:
  bool RunNextLocked(std::unique_lock<std::mutex>& lock) override {
    if (_items.empty()) {
      return false;
    }
    {
      // The item is destroyed before the lock is reacquired, so that its destructor
      // may call back into this queue
      T item = std::move(_items.front());
      _items.pop_front();
      lock.unlock();
      _handler(item);
    }
    lock.lock();
    return true;
  }

  void DiscardLocked(std::unique_lock<std::mutex>& lock) override {
    std::deque<T> discarded;
    discarded.swap(_items);
    lock.unlock();
    discarded.clear();
    lock.lock();
  }

  bool HasItemsLocked() const override { return !_items.empty(); }

 private:
  // Only read by the worker running this queue, and fixed at construction
  const std::function<void(T&)> _handler;
  std::deque<T> _items;
};

/**
 * Serial queue of arbitrary functions, which can also run a function after a delay.
 */
class TaskQueueCore final : public QueueCore {
 public:
  using TimerId = uint64_t;

  TaskQueueCore(std::shared_ptr<ExecutorState> executor, const QueueOptions& options);

  bool Push(std::function<void()>&& task);
  bool PushDelayedTask(std::function<void()>&& task) override {
    return Push(std::move(task));
  }
  std::optional<TimerId> PostAfter(Duration delay, std::function<void()> task);
  bool CancelTimer(TimerId id);

 protected:
  bool RunNextLocked(std::unique_lock<std::mutex>& lock) override;
  void DiscardLocked(std::unique_lock<std::mutex>& lock) override;
  bool HasItemsLocked() const override { return !_items.empty(); }

 private:
  std::deque<std::function<void()>> _items;
};

}  // namespace executor_detail

/**
 * Handle to a serial queue: a FIFO of items that run one at a time, never concurrently
 * with each other, on whichever of the executor's worker threads is free. Successive
 * items may run on different threads; each item observes all memory effects of the
 * items before it.
 *
 * Push(), Close(), WaitIdle() and Resume() are thread-safe.
 *
 * Destroying the handle closes the queue and waits until it's idle, as Close() plus
 * WaitIdle() would. If the handle is destroyed from one of the queue's own items, it
 * instead drops all items that haven't started, whatever the stop policy.
 */
template <typename T>
class SerialQueue {
 public:
  explicit SerialQueue(std::shared_ptr<executor_detail::TypedQueueCore<T>> core)
      : _core(std::move(core)) {}

  // See ~TaskQueue() for why exception-escape is suppressed
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~SerialQueue() { executor_detail::QueueCore::ReleaseFromHandle(*_core); }

  SerialQueue(const SerialQueue&) = delete;
  SerialQueue& operator=(const SerialQueue&) = delete;
  SerialQueue(SerialQueue&&) = delete;
  SerialQueue& operator=(SerialQueue&&) = delete;

  /**
   * Appends an item to the back of the queue without blocking. Returns false, without
   * moving from `item`, if the queue is closed. Never runs the item on the calling
   * thread.
   */
  bool Push(T&& item) { return _core->Push(std::move(item)); }

  /**
   * Stops accepting new items, without blocking. What happens to items already queued
   * depends on the queue's StopPolicy. Idempotent.
   */
  void Close() { _core->Close(); }

  /**
   * Blocks until no item is queued or running. Must not be called from one of this
   * queue's own items, or from an item of another queue when items of that queue may
   * be waiting for the same worker.
   *
   * If the executor can't spawn any worker thread, logs an error and returns without
   * running the queue's items, rather than blocking forever.
   */
  void WaitIdle() { _core->WaitIdle(); }

  /**
   * Starts running items in a queue created with `start_suspended`. Idempotent.
   */
  void Resume() { _core->Resume(); }

  /**
   * Returns true if the calling thread is currently running one of this queue's items.
   */
  bool IsCurrent() const { return _core->IsCurrent(); }

 private:
  std::shared_ptr<executor_detail::TypedQueueCore<T>> _core;
};

/**
 * Serial queue of functions, which can also run a function on the queue after a delay.
 * Same guarantees as SerialQueue.
 */
class TaskQueue final {
 public:
  using TimerId = executor_detail::TaskQueueCore::TimerId;

  explicit TaskQueue(std::shared_ptr<executor_detail::TaskQueueCore> core)
      : _core(std::move(core)) {}

  ~TaskQueue();

  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;
  TaskQueue(TaskQueue&&) = delete;
  TaskQueue& operator=(TaskQueue&&) = delete;

  /** See SerialQueue::Push(). */
  bool Push(std::function<void()>&& task) { return _core->Push(std::move(task)); }

  /**
   * Pushes `task` onto this queue once `delay` has elapsed, measured on a monotonic
   * clock. The task then waits its turn behind any items already queued, so it may run
   * later than requested when the queue or the executor is busy.
   *
   * Returns an ID that can cancel the task before it's pushed, or std::nullopt if the
   * queue is closed. Closing the queue cancels all of its pending delayed tasks.
   */
  std::optional<TimerId> PostAfter(Duration delay, std::function<void()> task) {
    return _core->PostAfter(delay, std::move(task));
  }

  /**
   * Cancels a delayed task that hasn't been pushed yet, destroying it. Returns false if
   * no such task is pending.
   */
  bool CancelTimer(TimerId id) { return _core->CancelTimer(id); }

  /** See SerialQueue::Close(). */
  void Close() { _core->Close(); }

  /** See SerialQueue::WaitIdle(). */
  void WaitIdle() { _core->WaitIdle(); }

  /** See SerialQueue::IsCurrent(). */
  bool IsCurrent() const { return _core->IsCurrent(); }

 private:
  std::shared_ptr<executor_detail::TaskQueueCore> _core;
};

/**
 * Runs the SDK's background work on a bounded set of worker threads that it spawns
 * and owns.
 *
 * Work is submitted to serial queues created from the executor. A queue with pending
 * work occupies at most one worker at a time, so a slow item only ever holds up its
 * own queue and one thread. Workers are spawned lazily, when a queue has work and no
 * worker is free, up to the thread limit: by default, one worker per live queue, which
 * gives each kind of background work its own thread, as if each owned a dedicated
 * thread. When the limit is lower than the number of queues with work, workers take
 * turns between queues, and a queue yields its worker after a short time budget
 * whenever other work is waiting.
 *
 * Delayed work (TaskQueue::PostAfter) is tracked by the executor and fired by its own
 * workers, so it needs no extra thread.
 *
 * The thread limit only covers the workers this executor spawns. It does not apply to
 * threads created by other components, such as an HTTP library's internal threads or
 * the crash reporting handler.
 *
 * All methods are thread-safe. Shutdown() and the destructor must not be called from a
 * worker thread.
 */
class Executor final {
 public:
  /**
   * Creates an executor; spawns no threads until work is submitted.
   *
   * @param max_worker_threads Maximum number of worker threads that this executor may
   *  spawn. 0 means one per live queue.
   * @param diagnostic_logger Reports failures to spawn threads.
   */
  explicit Executor(
      uint32_t max_worker_threads = 0, DiagnosticLogger diagnostic_logger = {}
  );

  /** Equivalent to Shutdown(). */
  ~Executor();

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;
  Executor(Executor&&) = delete;
  Executor& operator=(Executor&&) = delete;

  /**
   * Creates a serial queue whose items are passed to `handler`, one at a time.
   */
  template <typename T>
  std::unique_ptr<SerialQueue<T>> CreateQueue(
      const QueueOptions& options, std::function<void(T&)> handler
  ) {
    auto core = std::make_shared<executor_detail::TypedQueueCore<T>>(
        _state, options, std::move(handler)
    );
    Register(core);
    return std::make_unique<SerialQueue<T>>(std::move(core));
  }

  /**
   * Creates a serial queue of functions.
   */
  std::unique_ptr<TaskQueue> CreateTaskQueue(const QueueOptions& options);

  /**
   * Closes every live queue whose stop policy is DiscardPending, without blocking.
   * Lets shutdown cancel all skippable work up front, before waiting for essential work
   * to drain.
   */
  void CloseDiscardableQueues();

  /**
   * Stops all workers once the runnable work is done, cancels pending delayed tasks,
   * and joins every worker thread. Queues should be closed and idle beforehand: work
   * pushed afterwards never runs. Idempotent.
   */
  void Shutdown();

  /**
   * Returns the number of worker threads spawned so far.
   */
  size_t GetWorkerCount() const;

  /**
   * Returns true if the calling thread is a worker thread of any Executor.
   */
  static bool IsWorkerThread();

 private:
  void Register(const std::shared_ptr<executor_detail::QueueCore>& core);

  std::shared_ptr<executor_detail::ExecutorState> _state;
};

}  // namespace datadog::impl
