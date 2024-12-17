// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.
#include <datadog/internal/owning_blocking_queue.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "datadog/datadog_test.h"

// This file is full of magic numbers according to clang-tidy,
// quiet it down a bit
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)

namespace {

using datadog::core::internal::OwningBlockingQueue;
using namespace std::chrono_literals;

template <typename T>
class ThreadedConsumer {
 public:
  // Note: For the purposes of testing, accept a naked pointer,
  // but we would never do this in practice.
  explicit ThreadedConsumer(OwningBlockingQueue<T>* queue)
      : running{false}, queue{queue} {}

  bool WaitUntilStarted() {
    std::unique_lock<std::mutex> lock(lock_);
    if (running) {
      return true;
    }
    return started_cv_.wait_for(lock, 200ms) == std::cv_status::no_timeout;
  }

  static void ThreadProc(ThreadedConsumer<T>* consumer) { consumer->run(); }

  volatile bool running;
  OwningBlockingQueue<T>* queue;
  T last_object;

 private:
  void run() {
    running = true;
    started_cv_.notify_one();
    bool has_values = true;
    while (has_values) {
      auto opt_val = queue->GetNext();
      if (opt_val.has_value()) {
        last_object = std::move(opt_val.value());
      } else {
        has_values = !queue->IsEmpty();
      }
    }
    running = false;
  }

  std::mutex lock_;
  std::condition_variable started_cv_;
};

class MoveOnly {
 public:
  MoveOnly() : property{""} { construct_count++; };
  MoveOnly(MoveOnly&&) = default;
  MoveOnly& operator=(MoveOnly&&) = default;
  ~MoveOnly() = default;
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly& operator=(const MoveOnly&) = delete;

  explicit MoveOnly(const std::string& prop) : property{prop} {
    construct_count++;
  }

  std::string property;

  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static int construct_count;
};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
int MoveOnly::construct_count = 0;

TEST_CASE("M block W GetNext", "[blocking_queue]") {
  // Given
  OwningBlockingQueue<MoveOnly> queue;
  ThreadedConsumer consumer{&queue};
  std::thread t(ThreadedConsumer<MoveOnly>::ThreadProc, &consumer);
  REQUIRE(consumer.WaitUntilStarted());

  // When
  std::this_thread::sleep_for(500ms);

  // Then
  // Havent're received a value yet
  REQUIRE(consumer.last_object.property == "");
  REQUIRE(consumer.running);

  // Shutdown
  queue.Shutdown();
  t.join();
}

TEST_CASE("M get first value W GetNext", "[blocking_queue]") {
  // Given
  OwningBlockingQueue<MoveOnly> queue;
  queue.Push(MoveOnly("first_value"));

  ThreadedConsumer consumer{&queue};
  std::thread t(ThreadedConsumer<MoveOnly>::ThreadProc, &consumer);
  REQUIRE(consumer.WaitUntilStarted());

  // When
  std::this_thread::sleep_for(50ms);

  // Then
  REQUIRE(consumer.last_object.property == "first_value");

  // Shutdown
  queue.Shutdown();
  t.join();
}

TEST_CASE("M block after first value W GetNext", "[blocking_queue]") {
  // Given
  OwningBlockingQueue<MoveOnly> queue;
  queue.Push(MoveOnly("first_value"));

  ThreadedConsumer consumer{&queue};
  std::thread t(ThreadedConsumer<MoveOnly>::ThreadProc, &consumer);
  REQUIRE(consumer.WaitUntilStarted());

  // When
  std::this_thread::sleep_for(100ms);
  REQUIRE(consumer.last_object.property == "first_value");
  queue.Push(MoveOnly("second_value"));

  // Then
  queue.Shutdown();
  t.join();
  REQUIRE(consumer.last_object.property == "second_value");
}

TEST_CASE("M allow getting all remaining objects W GetNext { shutdown }",
          "[blocking_queue]") {
  // Given
  OwningBlockingQueue<MoveOnly> queue;
  queue.Push(MoveOnly("first_value"));
  ThreadedConsumer consumer{&queue};
  std::thread t(ThreadedConsumer<MoveOnly>::ThreadProc, &consumer);
  REQUIRE(consumer.WaitUntilStarted());

  // When
  queue.Push(MoveOnly("first_value"));
  queue.Push(MoveOnly("second_value"));
  queue.Push(MoveOnly("third_value"));
  queue.Push(MoveOnly("fourth_value"));
  queue.Shutdown();
  t.join();

  // Then
  REQUIRE(consumer.last_object.property == "fourth_value");
}

TEST_CASE("M be move only W Push & GetNext", "[blocking_queue]") {
  // Given
  MoveOnly::construct_count = 0;

  OwningBlockingQueue<MoveOnly> queue;
  ThreadedConsumer consumer{&queue};
  std::thread t(ThreadedConsumer<MoveOnly>::ThreadProc, &consumer);
  REQUIRE(consumer.WaitUntilStarted());

  // When
  queue.Push(MoveOnly("first_value"));
  queue.Shutdown();
  t.join();

  // Then
  REQUIRE(consumer.last_object.property == "first_value");
  // One for the default initial value, one for the pushed object
  REQUIRE(MoveOnly::construct_count == 2);
}

TEST_CASE("M return false W Push { shutdown }", "[blocking_queue]") {
  // Given
  OwningBlockingQueue<MoveOnly> queue;
  ThreadedConsumer consumer{&queue};
  std::thread t(ThreadedConsumer<MoveOnly>::ThreadProc, &consumer);
  REQUIRE(consumer.WaitUntilStarted());

  queue.Push(MoveOnly("first_value"));
  queue.Shutdown();

  // When
  REQUIRE(!queue.Push(MoveOnly("second_value")));

  // Then
  t.join();
  REQUIRE(consumer.last_object.property == "first_value");
}

}  // namespace

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
