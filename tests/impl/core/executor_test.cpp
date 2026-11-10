// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/executor.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <chrono>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <pthread.h>
#endif

#include "support/threading.hpp"

using namespace datadog;
using namespace datadog::impl;
using namespace std::chrono_literals;

template <typename Predicate>
static bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout = 5s) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) {
      return false;
    }
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

// Records whether two items of the same queue ever run at the same time
struct OverlapDetector {
  std::atomic<int> in_flight{0};
  std::atomic<bool> overlapped{false};

  void Enter() {
    if (in_flight.fetch_add(1) != 0) {
      overlapped = true;
    }
  }
  void Exit() { in_flight.fetch_sub(1); }
};

TEST_CASE("Executor serial queues", "[unit][core]") {
  const uint32_t max_worker_threads = GENERATE(0u, 1u, 2u, 4u);
  CAPTURE(max_worker_threads);
  Executor executor(max_worker_threads);

  SECTION(
      "M run items one at a time in FIFO order W many producers push concurrently"
  ) {
    // Given a typed queue whose handler records the order of items and checks that no
    // two of them overlap
    constexpr size_t num_producers = 4;
    constexpr size_t items_per_producer = 500;
    OverlapDetector detector;
    std::vector<std::vector<size_t>> received(num_producers);
    auto queue = executor.CreateQueue<std::pair<size_t, size_t>>(
        {"test", StopPolicy::Drain}, [&](std::pair<size_t, size_t>& item) {
          detector.Enter();
          // Only this queue's items touch `received`, so no lock: a data race here
          // would mean items ran concurrently
          received[item.first].push_back(item.second);
          detector.Exit();
        }
    );

    // When several threads push items at the same time
    auto threads = RunParallel(num_producers, [&](size_t producer) {
      for (size_t i = 0; i < items_per_producer; i++) {
        REQUIRE(queue->Push({producer, i}));
      }
    });
    for (auto& t : threads) {
      t.join();
    }
    queue->Close();
    queue->WaitIdle();

    // Then every item ran, never concurrently, and each producer's items ran in the
    // order that producer pushed them
    REQUIRE_FALSE(detector.overlapped);
    for (const auto& items : received) {
      REQUIRE(items.size() == items_per_producer);
      for (size_t i = 0; i < items.size(); i++) {
        REQUIRE(items[i] == i);
      }
    }
  }

  SECTION("M never run an item on the pushing thread W pushed from a worker") {
    // Given two task queues
    auto queue_a = executor.CreateTaskQueue({"a", StopPolicy::Drain});
    auto queue_b = executor.CreateTaskQueue({"b", StopPolicy::Drain});
    std::promise<bool> b_ran_inline;

    // When an item on A pushes an item onto B
    queue_a->Push([&] {
      std::atomic<bool> pushing{true};
      queue_b->Push([&b_ran_inline, &pushing] {
        b_ran_inline.set_value(pushing.load());
      });
      pushing = false;
    });

    // Then B's item ran after the push returned, not inside it
    auto future = b_ran_inline.get_future();
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    REQUIRE_FALSE(future.get());
    queue_a->Close();
    queue_a->WaitIdle();
    queue_b->Close();
    queue_b->WaitIdle();
  }

  SECTION("M spawn no more workers than the limit or the number of queues") {
    // Given no work yet, no worker exists
    REQUIRE(executor.GetWorkerCount() == 0);

    // When three queues each run a blocking item at the same time
    constexpr size_t num_queues = 3;
    std::vector<std::unique_ptr<TaskQueue>> queues;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<int> started{0};
    for (size_t i = 0; i < num_queues; i++) {
      queues.push_back(executor.CreateTaskQueue({"test", StopPolicy::Drain}));
      queues.back()->Push([&started, released] {
        started++;
        released.wait();
      });
    }

    // Then as many run in parallel as the limit allows
    const size_t expected_parallel =
        max_worker_threads == 0 ? num_queues
                                : std::min<size_t>(max_worker_threads, num_queues);
    REQUIRE(WaitFor([&] {
      return started.load() == static_cast<int>(expected_parallel);
    }));
    std::this_thread::sleep_for(20ms);
    REQUIRE(started.load() == static_cast<int>(expected_parallel));
    REQUIRE(executor.GetWorkerCount() == expected_parallel);

    // And once released, all of them run, without spawning more workers
    release.set_value();
    for (auto& queue : queues) {
      queue->Close();
      queue->WaitIdle();
    }
    REQUIRE(started.load() == static_cast<int>(num_queues));
    REQUIRE(executor.GetWorkerCount() == expected_parallel);
  }

  SECTION("M let other queues run between items W one queue has a long backlog") {
    // Given a queue with a long backlog of slow items
    auto busy = executor.CreateTaskQueue({"busy", StopPolicy::Drain});
    constexpr int num_busy_items = 200;
    std::atomic<int> busy_done{0};
    for (int i = 0; i < num_busy_items; i++) {
      busy->Push([&] {
        std::this_thread::sleep_for(1ms);
        busy_done++;
      });
    }

    // When an item is pushed onto another queue
    auto other = executor.CreateTaskQueue({"other", StopPolicy::Drain});
    std::atomic<int> busy_done_when_other_ran{-1};
    other->Push([&] { busy_done_when_other_ran = busy_done.load(); });
    other->Close();
    other->WaitIdle();

    // Then it runs long before the backlog is done, even with a single worker
    REQUIRE(busy_done_when_other_ran.load() >= 0);
    REQUIRE(busy_done_when_other_ran.load() < num_busy_items / 2);
    busy->Close();
    busy->WaitIdle();
    REQUIRE(busy_done.load() == num_busy_items);
  }

  SECTION("M report the current queue W asked from inside and outside an item") {
    auto queue = executor.CreateTaskQueue({"test", StopPolicy::Drain});
    auto other = executor.CreateTaskQueue({"other", StopPolicy::Drain});
    std::promise<std::pair<bool, bool>> observed;
    std::promise<bool> on_worker;
    queue->Push([&] {
      observed.set_value({queue->IsCurrent(), other->IsCurrent()});
      on_worker.set_value(Executor::IsWorkerThread());
    });
    REQUIRE(observed.get_future().get() == std::pair<bool, bool>{true, false});
    REQUIRE(on_worker.get_future().get());
    REQUIRE_FALSE(queue->IsCurrent());
    REQUIRE_FALSE(Executor::IsWorkerThread());
    queue->Close();
    queue->WaitIdle();
  }
}

TEST_CASE("Executor stop policies", "[unit][core]") {
  const uint32_t max_worker_threads = GENERATE(0u, 1u);
  CAPTURE(max_worker_threads);
  Executor executor(max_worker_threads);

  // A queue whose first item blocks until released, with more items queued behind it
  std::promise<void> started;
  std::promise<void> release;
  std::atomic<int> ran{0};
  auto fill = [&](TaskQueue& queue) {
    queue.Push([&, released = release.get_future().share()] {
      started.set_value();
      released.wait();
      ran++;
    });
    for (int i = 0; i < 10; i++) {
      queue.Push([&] { ran++; });
    }
    REQUIRE(started.get_future().wait_for(5s) == std::future_status::ready);
  };

  SECTION("M run every accepted item W Drain queue closed") {
    auto queue = executor.CreateTaskQueue({"drain", StopPolicy::Drain});
    fill(*queue);

    // When the queue is closed while its first item is running
    queue->Close();
    REQUIRE_FALSE(queue->Push([&] { ran++; }));
    release.set_value();
    queue->WaitIdle();

    // Then every item accepted before Close() ran, and none after
    REQUIRE(ran.load() == 11);
  }

  SECTION("M run only the item in progress W DiscardPending queue closed") {
    auto queue = executor.CreateTaskQueue({"discard", StopPolicy::DiscardPending});
    fill(*queue);

    // When the queue is closed while its first item is running
    queue->Close();
    REQUIRE_FALSE(queue->Push([&] { ran++; }));
    release.set_value();
    queue->WaitIdle();

    // Then only the item that was already running completed
    REQUIRE(ran.load() == 1);
  }

  SECTION("M close only discardable queues W CloseDiscardableQueues() called") {
    auto drain = executor.CreateTaskQueue({"drain", StopPolicy::Drain});
    auto discard = executor.CreateTaskQueue({"discard", StopPolicy::DiscardPending});

    executor.CloseDiscardableQueues();

    REQUIRE(drain->Push([] {}));
    REQUIRE_FALSE(discard->Push([] {}));
    REQUIRE_FALSE(discard->PostAfter(1ms, [] {}).has_value());
    drain->Close();
    drain->WaitIdle();
  }

  SECTION("M hold items until resumed W queue created suspended") {
    auto queue = executor.CreateQueue<int>(
        {"suspended", StopPolicy::Drain, /*start_suspended=*/true}, [&](int&) { ran++; }
    );
    queue->Push(1);
    queue->Push(2);
    std::this_thread::sleep_for(20ms);
    REQUIRE(ran.load() == 0);

    queue->Resume();
    REQUIRE(WaitFor([&] { return ran.load() == 2; }));
    queue->Close();
    queue->WaitIdle();
  }

  SECTION("M drain held items W suspended Drain queue closed") {
    auto queue = executor.CreateQueue<int>(
        {"suspended", StopPolicy::Drain, /*start_suspended=*/true}, [&](int&) { ran++; }
    );
    queue->Push(1);
    queue->Close();
    queue->WaitIdle();
    REQUIRE(ran.load() == 1);
  }

  SECTION("M not deadlock W queue destroyed from its own item") {
    // Given a queue that destroys itself from one of its items, with more items behind
    auto queue = executor.CreateTaskQueue({"self", StopPolicy::Drain});
    std::promise<void> destroyed;
    std::promise<void> both_pushed;
    queue->Push([&, pushed = both_pushed.get_future().share()] {
      pushed.wait();
      queue.reset();
      destroyed.set_value();
    });
    queue->Push([&] { ran++; });
    both_pushed.set_value();

    // Then destruction returns, and the items behind it are dropped
    REQUIRE(destroyed.get_future().wait_for(5s) == std::future_status::ready);
    std::this_thread::sleep_for(20ms);
    REQUIRE(ran.load() == 0);
  }
}

TEST_CASE("Executor delayed tasks", "[unit][core]") {
  const uint32_t max_worker_threads = GENERATE(0u, 1u);
  CAPTURE(max_worker_threads);
  Executor executor(max_worker_threads);
  auto queue = executor.CreateTaskQueue({"timers", StopPolicy::DiscardPending});

  SECTION("M run task after its delay W posted") {
    std::promise<std::chrono::steady_clock::time_point> ran_at;
    const auto posted_at = std::chrono::steady_clock::now();
    REQUIRE(queue->PostAfter(30ms, [&] {
      ran_at.set_value(std::chrono::steady_clock::now());
    }));
    auto future = ran_at.get_future();
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    REQUIRE(future.get() - posted_at >= 30ms);
  }

  SECTION("M run tasks in deadline order W posted out of order") {
    std::mutex mutex;
    std::vector<int> order;
    auto record = [&](int value) {
      return [&, value] {
        std::lock_guard lock(mutex);
        order.push_back(value);
      };
    };
    queue->PostAfter(40ms, record(3));
    queue->PostAfter(10ms, record(1));
    queue->PostAfter(25ms, record(2));
    REQUIRE(WaitFor([&] {
      std::lock_guard lock(mutex);
      return order.size() == 3;
    }));
    REQUIRE(order == std::vector<int>{1, 2, 3});
  }

  SECTION("M never run task W cancelled before its delay") {
    std::atomic<bool> ran{false};
    auto id = queue->PostAfter(20ms, [&] { ran = true; });
    REQUIRE(id.has_value());
    REQUIRE(queue->CancelTimer(*id));
    REQUIRE_FALSE(queue->CancelTimer(*id));
    std::this_thread::sleep_for(50ms);
    REQUIRE_FALSE(ran);
  }

  SECTION("M never run task W queue closed before its delay") {
    std::atomic<bool> ran{false};
    REQUIRE(queue->PostAfter(20ms, [&] { ran = true; }));
    queue->Close();
    std::this_thread::sleep_for(50ms);
    REQUIRE_FALSE(ran);
    REQUIRE_FALSE(queue->PostAfter(1ms, [] {}).has_value());
  }

  SECTION("M fire task W the only worker is busy with another queue's backlog") {
    // Given a backlog of slow items on another queue
    auto busy = executor.CreateTaskQueue({"busy", StopPolicy::Drain});
    constexpr int num_busy_items = 200;
    std::atomic<int> busy_done{0};
    for (int i = 0; i < num_busy_items; i++) {
      busy->Push([&] {
        std::this_thread::sleep_for(1ms);
        busy_done++;
      });
    }

    // When a delayed task comes due during the backlog
    std::atomic<int> busy_done_when_fired{-1};
    queue->PostAfter(10ms, [&] { busy_done_when_fired = busy_done.load(); });

    // Then it runs long before the backlog is done
    REQUIRE(WaitFor([&] { return busy_done_when_fired.load() >= 0; }));
    REQUIRE(busy_done_when_fired.load() < num_busy_items / 2);
    busy->Close();
    busy->WaitIdle();
  }
}

TEST_CASE("Executor delayed task with several idle workers", "[unit][core]") {
  // Given an executor with two workers, both idle
  Executor executor;
  auto busy = executor.CreateTaskQueue({"busy", StopPolicy::Drain});
  auto other = executor.CreateTaskQueue({"other", StopPolicy::Drain});
  auto timers = executor.CreateTaskQueue({"timers", StopPolicy::DiscardPending});
  {
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<int> started{0};
    busy->Push([&started, released] {
      started++;
      released.wait();
    });
    other->Push([&started, released] {
      started++;
      released.wait();
    });
    REQUIRE(WaitFor([&] { return started.load() == 2; }));
    release.set_value();
    busy->WaitIdle();
    other->WaitIdle();
  }
  REQUIRE(executor.GetWorkerCount() == 2);

  // When a delayed task is posted, and then one queue keeps a worker busy until long
  // after the task is due
  std::atomic<int> busy_done{0};
  std::atomic<int> busy_done_when_fired{-1};
  timers->PostAfter(20ms, [&] { busy_done_when_fired = busy_done.load(); });
  constexpr int num_busy_items = 300;
  for (int i = 0; i < num_busy_items; i++) {
    busy->Push([&] {
      std::this_thread::sleep_for(1ms);
      busy_done++;
    });
  }

  // Then the task fires on time, on whichever worker is still idle
  REQUIRE(WaitFor([&] { return busy_done_when_fired.load() >= 0; }));
  REQUIRE(busy_done_when_fired.load() < num_busy_items / 2);
  busy->Close();
  busy->WaitIdle();
}

TEST_CASE("Executor shutdown", "[unit][core]") {
  SECTION("M wait for thread-local destructors W shut down") {
    // Given a worker that has a slow thread_local destructor
    struct SlowCleanup {
      std::atomic<bool>* cleaned = nullptr;
      ~SlowCleanup() {
        if (cleaned != nullptr) {
          std::this_thread::sleep_for(50ms);
          *cleaned = true;
        }
      }
    };
    std::atomic<bool> cleaned{false};
    Executor executor;
    auto queue = executor.CreateTaskQueue({"test", StopPolicy::Drain});
    queue->Push([&] {
      thread_local SlowCleanup cleanup;
      cleanup.cleaned = &cleaned;
    });
    queue->Close();
    queue->WaitIdle();
    queue.reset();

    // When the executor shuts down
    executor.Shutdown();

    // Then the workers have fully exited, thread_local destructors included
    REQUIRE(cleaned);
  }

#if defined(__APPLE__) || defined(__linux__)
  SECTION("M name worker threads W running an item") {
    Executor executor;
    auto queue = executor.CreateTaskQueue({"test", StopPolicy::Drain});
    std::promise<std::string> name;
    queue->Push([&] {
      std::array<char, 32> buffer{};
      pthread_getname_np(pthread_self(), buffer.data(), buffer.size());
      name.set_value(buffer.data());
    });
    REQUIRE(name.get_future().get() == "dd-worker-1");
    queue->Close();
    queue->WaitIdle();
  }
#endif
}
