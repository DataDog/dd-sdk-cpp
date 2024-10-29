// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace datadog::core::internal {

// OwningBlockingQueue is a thread safe Queue that moves / owns objects that get
// put into it with Push, until they are removed with GetNext. If the queue is
// empty, GetNext blocks until an item is put into the queue or until the Queue
// is shutdown (returning an empty std::optional in this case).
template <typename T>
class OwningBlockingQueue {
 public:
  OwningBlockingQueue() : is_shutdown_{false} {}

  void Push(T&& element) {
    {
      std::lock_guard<std::mutex> lock{lock_};
      element_queue_.push(std::move(element));
    }
    signal_.notify_one();
  }

  bool IsEmpty() {
    std::lock_guard<std::mutex> lock(lock_);
    return element_queue_.empty();
  }

  std::optional<T> GetNext() {
    std::unique_lock<std::mutex> lock(lock_);
    if (!element_queue_.empty()) {
      std::optional<T> element(std::move(element_queue_.front()));
      element_queue_.pop();
      return element;
    }

    if (is_shutdown_) {
      return std::optional<T>();
    }

    signal_.wait(lock);
    if (element_queue_.empty()) {
      return std::optional<T>();
    }

    std::optional<T> element(std::move(element_queue_.front()));
    element_queue_.pop();

    return element;
  }

  bool IsShutdown() const { return is_shutdown_; }

  void Shutdown() {
    is_shutdown_ = true;
    signal_.notify_all();
  };

 private:
  volatile bool is_shutdown_;
  std::mutex lock_;
  std::condition_variable signal_;
  std::queue<T> element_queue_;
};

}  // namespace datadog::core::internal
