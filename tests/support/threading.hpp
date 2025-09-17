#pragma once

#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "support/threading_checks.hpp"

#if WITH_DATADOG_STRICT_THREADING_CHECKS
#define STRICT_THREADING_REQUIRE(...) REQUIRE(__VA_ARGS__)
#else
// clang-format off
#define STRICT_THREADING_REQUIRE(...) do { (void)(__VA_ARGS__); } while(0)
// clang-format on
#endif

/**
 * Starts and runs `n` threads, with each one running `func(i)`. Synchronizes the
 * threads such that they all begin running at roughly the same time.
 */
inline std::vector<std::thread> RunParallel(
    size_t n, std::function<void(size_t)> func
) {
  // Once we resolve start_promise, start_signal will fire and unblock all threads
  std::promise<void> start_promise;
  std::shared_future<void> start_signal = start_promise.get_future().share();

  // Each thread will signal its readiness by incrementing num_ready, synchronized
  // using the same mutex that synchronizes notifications to the outer function via a
  // condition_variable
  std::mutex mu;
  std::condition_variable cv;
  size_t num_ready = 0;

  // For our thread entry-point, wrap the provided function with synchronization logic
  // that ensures everyone is lined up at the starting line
  auto thread_main = [&mu, &cv, &num_ready, start_signal, func](size_t i) {
    // Announce that this thread is ready
    {
      std::lock_guard lock(mu);
      num_ready++;
      cv.notify_one();
    }

    // Wait until we get the signal that all threads are ready
    start_signal.wait();

    // Do whatever actual work the test requires this thread to do
    func(i);
  };

  // Create N threads to run our wrapper function
  std::vector<std::thread> threads;
  threads.reserve(n);
  for (size_t i = 0; i < n; i++) {
    threads.emplace_back(thread_main, i);
  }

  // Wait until all threads are blocked waiting for start_signal
  {
    std::unique_lock lock(mu);
    cv.wait(lock, [&]() { return num_ready == n; });
  }

  // Signal to all threads that they can begin their work
  start_promise.set_value();

  // Move ownership of the std::thread vector to the caller: this allows the test to do
  // work in the main thread (concurrent with all threads) before joining
  return threads;
}

/**
 * Behaves identically to RunParallel, except all functions are run synchronously, one
 * after the other, in the main thread.
 */
inline std::vector<std::thread> RunSequential(
    size_t n, std::function<void(size_t)> func
) {
  for (size_t i = 0; i < n; i++) {
    func(i);
  }
  return std::vector<std::thread>{};
}
