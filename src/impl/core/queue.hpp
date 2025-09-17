#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

#include "assert.hpp"

namespace datadog::impl {

/**
 * Thread-safe FIFO queue that allows messages to be passed between threads. Uses move
 * semantics to avoid copies: items successfully pushed onto the queue are owned by the
 * queue, and ownership is transferred to the consumer once an item is popped.
 *
 * `Push()` is non-blocking and may be invoked from any thread, including the main
 * thread. A return value of false from `Push()` indicates that queue processing has
 * stopped and no new items will be accepted.
 *
 * `Pop()` is blocking: a consumer thread should call `Pop()` continually, blocking on
 * each call until an item is ready. A return value of std::nullopt from `Pop()`
 * indicates that queue processing has been stopped and all items have been processed.
 *
 * `Stop()` triggers graceful shutdown: no further items will be accepted from
 * producers, and consumers will be allowed to continue reading items until the queue is
 * drained.
 *
 * The owner of the Queue MUST call `Stop()`, and then join on all threads that use it,
 * before the Queue leaves scope.
 */
template <typename T>
class Queue {
 public:
  Queue() : _is_stopped(false) {}

  ~Queue() {
    // A call to Stop() here would represent a race condition, as stopping queue
    // processing involves synchronization with threads that the queue itself does not
    // own. The owner of the Queue MUST call Queue::Stop() explicitly, and join on all
    // threads that use it, before the Queue goes out of scope.
    DATADOG_ASSERT(
        _is_stopped.load(), "Queue destructor called while _is_stopped true"
    );
  }

  Queue(const Queue&) = delete;
  Queue& operator=(const Queue&) = delete;
  Queue(Queue&&) = delete;
  Queue& operator=(Queue&&) = delete;

  /**
   * Adds a new item to the back of the queue, without blocking. Will succeed if the
   * queue is active; will fail and return false if queue processing has stopped.
   *
   * @param item rvalue reference to the new item, which will be moved into the queue
   *  if successful. If unsuccessful; no move will occur.
   * @returns success.
   */
  bool Push(T&& item) {
    // Synchronize writes with reads
    std::lock_guard<std::mutex> lock(_mutex);

    // Check to see if queue processing has been stopped: relaxed memory ordering is
    // sufficient since mutex acquisition provides full sequential consistency
    if (_is_stopped.load(std::memory_order_relaxed)) {
      // We're shut down; reject the item and return false
      return false;
    }

    // Move the item into the underlying deque: we now own this item
    _items.emplace_back(std::move(item));

    // Signal to consumers that new data is available for read
    _condition.notify_one();
    return true;
  }

// StorageMessage contains a union with a std::vector<uint8_t> member, and GCC has
// trouble understanding the initialization of the vector's internal pointers during
// move operations, so it erroneously flags it as usage of an uninitialized variable.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  /**
   * Retrieves an item from the front of the queue, blocking until either an item is
   * available or queue processing has stopped.
   *
   * @returns The next item to process, or `std::nullopt` if the queue is shut down and
   *  all items have been processed.
   */
  std::optional<T> Pop() {
    // Synchronize reads with writes
    std::unique_lock<std::mutex> lock(_mutex);

    // Wait until there are items available in the queue, or processing is stopped,
    // handling spurious wakeups
    _condition.wait(lock, [this]() {
      // Relaxed ordering is also fine here because condition_variable reacquires the
      // lock each time it evaluates this condition
      return !_items.empty() || _is_stopped.load(std::memory_order_relaxed);
    });

    // If we're awake with zero items in the queue, it's because queue processing has
    // stopped and we've drained all remaining items: return nullopt to signal that it's
    // time to exit
    if (_items.empty()) {
      return std::nullopt;
    }

    // Queue is not empty: move the first item out of the queue, transferring ownership
    // to this local variable
    T item = std::move(_items.front());
    _items.pop_front();

    // Finally, transfer ownership of the item to the caller: NRVO will elide the copy
    return item;
  }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  /**
   * Stops all queue processing. Subsequent calls to `Push()` will reject the operation
   * and return false, and all pending `Pop()` calls will eventually return with
   * `std::nullopt` once the queue is drained.
   */
  void Stop() {
    // Set our atomic flag with memory_order_release to ensure that the change is
    // visible to all threads
    _is_stopped.store(true, std::memory_order_release);

    // Signal to consumers that it's time to shut down
    _condition.notify_all();
  }

 private:
  mutable std::mutex _mutex;
  std::condition_variable _condition;
  std::deque<T> _items;
  std::atomic<bool> _is_stopped;
};

}  // namespace datadog::impl
