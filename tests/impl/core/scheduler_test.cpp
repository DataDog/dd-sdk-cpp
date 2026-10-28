// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/core/scheduler.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace datadog;
using namespace datadog::impl;
using namespace std::chrono_literals;

// Polls until the predicate holds or the timeout expires
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

// Calls the given callback when destroyed. Captured (by shared_ptr) in a task closure,
// it makes the closure call back into the Scheduler at the moment it is destroyed,
// which deadlocks if that happens while the scheduler mutex is held
class CallOnDestroy {
 public:
  explicit CallOnDestroy(std::function<void()> on_destroy)
      : _on_destroy(std::move(on_destroy)) {}

  CallOnDestroy(const CallOnDestroy&) = delete;
  CallOnDestroy& operator=(const CallOnDestroy&) = delete;
  CallOnDestroy(CallOnDestroy&&) = delete;
  CallOnDestroy& operator=(CallOnDestroy&&) = delete;

  ~CallOnDestroy() { _on_destroy(); }

 private:
  std::function<void()> _on_destroy;
};

// Records the outcome of each run
class RecordingCallback : public Callback<int> {
 public:
  void OnSuccess(int result) override {
    last_result = result;
    successes++;
  }
  void OnFailure(std::exception_ptr error) override {
    if (error) {
      failures++;
    }
  }

  std::atomic<int> last_result{0};
  std::atomic<int> successes{0};
  std::atomic<int> failures{0};
};

// Counts the outcomes of a task without a result
class CountingCallback : public Callback<void> {
 public:
  void OnSuccess() override { successes++; }
  void OnFailure(std::exception_ptr error) override {
    if (error) {
      failures++;
    }
  }

  std::atomic<int> successes{0};
  std::atomic<int> failures{0};
};

// Schedules a task whose outcome the test doesn't care about
static std::optional<Scheduler::TaskId> ScheduleTask(
    Scheduler& scheduler, Duration period, std::function<void()> task
) {
  return scheduler.Schedule<void>(
      period, std::move(task), std::make_shared<CountingCallback>()
  );
}

TEST_CASE("Scheduler", "[unit]") {
  // Tasks and their closures capture section locals by reference, so those locals are
  // declared before the Scheduler: ~Scheduler is Shutdown(true), which waits for the
  // scheduler thread to exit and drops the closures, so being destroyed first it
  // guarantees nothing runs against a local that has left scope.
  SECTION("M run task repeatedly W task scheduled") {
    // Given a counter and a scheduler with a task scheduled every 5ms
    std::atomic<int> runs{0};
    Scheduler scheduler;
    auto id = ScheduleTask(scheduler, 5ms, [&] { runs++; });

    // Then the task gets an ID and runs repeatedly
    REQUIRE(id.has_value());
    REQUIRE(WaitFor([&] { return runs >= 3; }));
  }

  SECTION("M not run task before period elapses W long period") {
    // Given a counter and a scheduler with a task scheduled every 10 seconds
    std::atomic<int> runs{0};
    Scheduler scheduler;
    ScheduleTask(scheduler, 10s, [&] { runs++; });

    // When a short time passes
    std::this_thread::sleep_for(20ms);

    // Then the task hasn't run
    REQUIRE(runs == 0);
  }

  SECTION("M run earlier task W added after a later one") {
    // Given a counter and a scheduler waiting on a task due in 10 seconds
    std::atomic<int> runs{0};
    Scheduler scheduler;
    ScheduleTask(scheduler, 10s, [] {});
    std::this_thread::sleep_for(5ms);

    // When a task with a short period is added
    ScheduleTask(scheduler, 5ms, [&] { runs++; });

    // Then the scheduler wakes up and runs it
    REQUIRE(WaitFor([&] { return runs >= 1; }));
  }

  SECTION("M skip overrun ticks and keep the grid W a run overruns its period") {
    // Given a 100ms task whose first run takes 110ms, ending just past the 200ms tick
    std::atomic<int> runs{0};
    std::promise<std::chrono::steady_clock::time_point> second_run;
    auto second_run_future = second_run.get_future();
    const auto scheduled_at = std::chrono::steady_clock::now();
    Scheduler scheduler;
    ScheduleTask(scheduler, 100ms, [&] {
      const int run = ++runs;
      if (run == 1) {
        std::this_thread::sleep_for(110ms);
      } else if (run == 2) {
        second_run.set_value(std::chrono::steady_clock::now());
      }
    });

    // When the second run happens
    REQUIRE(second_run_future.wait_for(5s) == std::future_status::ready);

    // Then it waited for the first tick after the overrun (300ms) instead of running
    // right after it (~210ms); the wide gap absorbs sleep_for overshoot
    REQUIRE(second_run_future.get() - scheduled_at >= 300ms);
  }

  SECTION("M stop running task W cancelled") {
    // Given a counter and a task that has run at least once
    std::atomic<int> runs{0};
    Scheduler scheduler;
    auto id = ScheduleTask(scheduler, 1ms, [&] { runs++; });
    REQUIRE(WaitFor([&] { return runs >= 1; }));

    // When it's cancelled
    REQUIRE(scheduler.Cancel(*id));

    // Then it doesn't run anymore (allowing one in-flight run to finish)
    std::this_thread::sleep_for(10ms);
    const int runs_after_cancel = runs;
    std::this_thread::sleep_for(20ms);
    REQUIRE(runs == runs_after_cancel);

    // And cancelling again reports nothing to cancel
    REQUIRE_FALSE(scheduler.Cancel(*id));
  }

  SECTION("M keep other tasks running W one task cancelled") {
    // Given a pair of counters and two scheduled tasks
    std::atomic<int> runs_a{0};
    std::atomic<int> runs_b{0};
    Scheduler scheduler;
    auto id_a = ScheduleTask(scheduler, 1ms, [&] { runs_a++; });
    ScheduleTask(scheduler, 1ms, [&] { runs_b++; });

    // When the first is cancelled
    scheduler.Cancel(*id_a);
    const int runs_b_at_cancel = runs_b;

    // Then the second keeps running
    REQUIRE(WaitFor([&] { return runs_b >= runs_b_at_cancel + 3; }));
  }

  SECTION("M block until running task finishes W shutdown with wait") {
    // Given a task that is currently running and blocked
    std::promise<void> started;
    std::promise<void> release;
    std::atomic<bool> finished{false};
    Scheduler scheduler;
    ScheduleTask(scheduler, 1ms, [&, release_future = release.get_future().share()] {
      if (finished) {
        return;
      }
      started.set_value();
      release_future.wait();
      finished = true;
    });
    started.get_future().wait();

    // When we shut down with wait=true while the task is released shortly after
    std::thread releaser([&] {
      std::this_thread::sleep_for(20ms);
      release.set_value();
    });
    scheduler.Shutdown(true);
    const bool finished_when_shutdown_returned = finished;
    releaser.join();

    // Then Shutdown returned only after the task finished
    REQUIRE(finished_when_shutdown_returned);
  }

  SECTION("M wait for detached task W destroyed after shutdown without wait") {
    // Given a task that is currently running and blocked
    std::promise<void> started;
    std::promise<void> release;
    std::atomic<int> runs{0};
    std::atomic<bool> task_finished{false};
    auto scheduler = std::make_unique<Scheduler>();
    ScheduleTask(*scheduler, 1ms, [&, release_future = release.get_future().share()] {
      if (runs++ == 0) {
        started.set_value();
        release_future.wait();
        task_finished = true;
      }
    });
    started.get_future().wait();

    // When shutdown returns while the task is blocked, then destruction starts
    scheduler->Shutdown(false);
    const bool finished_at_shutdown = task_finished;
    std::promise<void> destroying;
    std::atomic<bool> destroyed{false};
    std::thread destroyer([&] {
      destroying.set_value();
      scheduler.reset();
      destroyed = true;
    });
    destroying.get_future().wait();
    std::this_thread::sleep_for(20ms);
    const bool destroyed_before_release = destroyed;

    // Then destruction waits until the in-progress run finishes
    release.set_value();
    const bool destruction_finished = WaitFor([&] { return destroyed.load(); });
    destroyer.join();
    const bool run_finished = WaitFor([&] { return task_finished.load(); });
    REQUIRE_FALSE(finished_at_shutdown);
    REQUIRE_FALSE(destroyed_before_release);
    REQUIRE(destruction_finished);
    REQUIRE(run_finished);
    REQUIRE(runs == 1);
  }

  SECTION("M wait for detached task W shutdown with wait after shutdown without wait") {
    // Given a detached scheduler thread with a blocked task
    std::promise<void> started;
    std::promise<void> release;
    std::atomic<bool> task_finished{false};
    Scheduler scheduler;
    ScheduleTask(scheduler, 1ms, [&, release_future = release.get_future().share()] {
      started.set_value();
      release_future.wait();
      task_finished = true;
    });
    started.get_future().wait();
    scheduler.Shutdown(false);

    // When a later shutdown waits while another thread releases the task
    std::atomic<bool> shutdown_finished{false};
    bool shutdown_before_release = false;
    std::thread releaser([&] {
      std::this_thread::sleep_for(20ms);
      shutdown_before_release = shutdown_finished;
      release.set_value();
    });
    scheduler.Shutdown(true);
    shutdown_finished = true;
    const bool finished_at_shutdown = task_finished;
    releaser.join();
    const bool run_finished = WaitFor([&] { return task_finished.load(); });

    // Then shutdown returned only after the task finished
    REQUIRE_FALSE(shutdown_before_release);
    REQUIRE(finished_at_shutdown);
    REQUIRE(run_finished);
  }

  SECTION("M wait for detached task W destroyed after shutdown from a task") {
    // Given a task that shuts down its scheduler and then remains blocked
    std::promise<void> stopped;
    std::promise<void> release;
    std::atomic<bool> task_finished{false};
    auto scheduler = std::make_unique<Scheduler>();
    ScheduleTask(*scheduler, 1ms, [&, release_future = release.get_future().share()] {
      scheduler->Shutdown(true);
      // A second call must still recognize the scheduler thread after detach
      scheduler->Shutdown(true);
      stopped.set_value();
      release_future.wait();
      task_finished = true;
    });
    stopped.get_future().wait();

    // When the test thread destroys the scheduler while the task is released later
    std::atomic<bool> destroyed{false};
    bool destroyed_before_release = false;
    std::thread releaser([&] {
      std::this_thread::sleep_for(20ms);
      destroyed_before_release = destroyed;
      release.set_value();
    });
    scheduler.reset();
    destroyed = true;
    const bool finished_at_destruction = task_finished;
    releaser.join();
    const bool run_finished = WaitFor([&] { return task_finished.load(); });

    // Then destruction returned only after the in-progress run finished
    REQUIRE_FALSE(destroyed_before_release);
    REQUIRE(finished_at_destruction);
    REQUIRE(run_finished);
  }

  SECTION("M wait for task W shut down from a task and the test thread concurrently") {
    // Given a task that shuts down its scheduler once the test thread starts doing so
    std::promise<void> started;
    std::promise<void> release;
    std::atomic<bool> task_finished{false};
    Scheduler scheduler;
    ScheduleTask(scheduler, 1ms, [&, release_future = release.get_future().share()] {
      started.set_value();
      release_future.wait();
      scheduler.Shutdown(false);
      task_finished = true;
    });
    started.get_future().wait();

    // When both threads shut it down at the same time
    std::thread releaser([&] {
      std::this_thread::sleep_for(5ms);
      release.set_value();
    });
    scheduler.Shutdown(true);
    const bool finished_at_shutdown = task_finished;
    releaser.join();

    // Then the waiting shutdown returns only after the task finished
    REQUIRE(finished_at_shutdown);
  }

  SECTION("M reject tasks W already shut down") {
    // Given a scheduler that has been shut down
    Scheduler scheduler;
    scheduler.Shutdown(true);

    // Then new tasks are rejected
    REQUIRE_FALSE(ScheduleTask(scheduler, 1ms, [] {}).has_value());
  }

  SECTION("M reject task W period is not positive") {
    // Given a scheduler
    Scheduler scheduler;

    // When a task is scheduled with a zero period
    auto id = ScheduleTask(scheduler, 0ms, [] {});

    // Then it is rejected
    REQUIRE_FALSE(id.has_value());
  }

  SECTION("M not deadlock W task cancelled while idle") {
    // Given a scheduler with a sentinel task and a task whose closure calls Cancel() on
    // that sentinel when the closure is destroyed
    std::atomic<bool> sentinel_ran{false};
    std::atomic<bool> destroyed{false};
    std::optional<Scheduler::TaskId> sentinel;
    Scheduler scheduler;
    sentinel = ScheduleTask(scheduler, 1ms, [&] { sentinel_ran = true; });
    auto cancel_sentinel_on_destroy = [&] {
      scheduler.Cancel(*sentinel);
      destroyed = true;
    };
    auto id = ScheduleTask(
        scheduler,
        10s,
        [helper = std::make_shared<CallOnDestroy>(cancel_sentinel_on_destroy)] {}
    );
    REQUIRE(id.has_value());
    REQUIRE(WaitFor([&] { return sentinel_ran.load(); }));

    // When the task is cancelled while the scheduler thread is idle
    REQUIRE(scheduler.Cancel(*id));

    // Then its closure was destroyed off the lock and its Cancel() went through
    REQUIRE(WaitFor([&] { return destroyed.load(); }));
  }

  SECTION("M not deadlock W shutdown drops a task whose closure calls Cancel") {
    // Given a scheduler with a sentinel task and a task whose closure calls Cancel() on
    // that sentinel when the closure is destroyed
    std::atomic<bool> destroyed{false};
    std::optional<Scheduler::TaskId> sentinel;
    Scheduler scheduler;
    sentinel = ScheduleTask(scheduler, 10s, [] {});
    auto cancel_sentinel_on_destroy = [&] {
      scheduler.Cancel(*sentinel);
      destroyed = true;
    };
    ScheduleTask(
        scheduler,
        10s,
        [helper = std::make_shared<CallOnDestroy>(cancel_sentinel_on_destroy)] {}
    );

    // When the scheduler is shut down while both tasks are still scheduled
    scheduler.Shutdown(true);

    // Then the closures were destroyed off the lock and the Cancel() went through
    REQUIRE(destroyed);
  }

  SECTION("M not deadlock W task cancels itself during its run") {
    // Given a task that cancels itself, which leaves the scheduler thread holding the
    // last reference to its closure, and whose closure calls Cancel() on a sentinel
    // task when the closure is destroyed
    std::atomic<int> sentinel_runs{0};
    std::atomic<Scheduler::TaskId> self_id{0};
    std::atomic<bool> destroyed{false};
    std::optional<Scheduler::TaskId> sentinel;
    Scheduler scheduler;
    sentinel = ScheduleTask(scheduler, 1ms, [&] { sentinel_runs++; });
    auto cancel_sentinel_on_destroy = [&] {
      scheduler.Cancel(*sentinel);
      destroyed = true;
    };
    auto id = ScheduleTask(
        scheduler,
        1ms,
        [&scheduler,
         &self_id,
         helper = std::make_shared<CallOnDestroy>(cancel_sentinel_on_destroy)] {
          if (const auto own = self_id.load(); own != 0) {
            scheduler.Cancel(own);
          }
        }
    );
    self_id = *id;

    // Then the closure was destroyed off the lock and its Cancel() stopped the sentinel
    REQUIRE(WaitFor([&] { return destroyed.load(); }));
    const int sentinel_runs_at_destroy = sentinel_runs;
    std::this_thread::sleep_for(20ms);
    REQUIRE(sentinel_runs == sentinel_runs_at_destroy);
  }

  SECTION("M reject task W period exceeds max") {
    // Given a scheduler
    Scheduler scheduler;

    // When tasks are scheduled with periods above MAX_PERIOD
    auto id_huge = ScheduleTask(scheduler, Duration::max(), [] {});
    auto id_just_over = ScheduleTask(scheduler, Scheduler::MAX_PERIOD + 1ns, [] {});

    // Then both are rejected
    REQUIRE_FALSE(id_huge.has_value());
    REQUIRE_FALSE(id_just_over.has_value());
  }

  SECTION("M accept task W period equals max") {
    // Given a scheduler
    Scheduler scheduler;

    // When a task is scheduled with a period of exactly MAX_PERIOD
    auto id = ScheduleTask(scheduler, Scheduler::MAX_PERIOD, [] {});

    // Then it is accepted
    REQUIRE(id.has_value());
  }

  SECTION("M not deadlock W shutdown called from a task") {
    // Given a flag and a task that shuts down its own scheduler
    std::atomic<bool> done{false};
    Scheduler scheduler;
    ScheduleTask(scheduler, 1ms, [&] {
      scheduler.Shutdown(true);
      done = true;
    });

    // Then it completes instead of deadlocking on a self-join
    REQUIRE(WaitFor([&] { return done.load(); }));
  }

  SECTION("M report result to OnSuccess W task returns") {
    // Given a scheduler with a task returning 42
    auto callback = std::make_shared<RecordingCallback>();
    Scheduler scheduler;
    scheduler.Schedule<int>(1ms, [] { return 42; }, callback);

    // Then the result reaches OnSuccess and OnFailure is never called
    REQUIRE(WaitFor([&] { return callback->successes >= 2; }));
    REQUIRE(callback->last_result == 42);
    REQUIRE(callback->failures == 0);
  }

  SECTION("M report exception to OnFailure W task throws") {
    // Given a scheduler with a task that always throws
    auto callback = std::make_shared<RecordingCallback>();
    Scheduler scheduler;
    scheduler.Schedule<int>(
        1ms, []() -> int { throw std::runtime_error("boom"); }, callback
    );

    // Then every run reaches OnFailure, the scheduler keeps going, OnSuccess never runs
    REQUIRE(WaitFor([&] { return callback->failures >= 2; }));
    REQUIRE(callback->successes == 0);
  }

  SECTION("M reject task W callback is null") {
    // Given a scheduler
    Scheduler scheduler;

    // When a task is scheduled without a callback
    auto id = scheduler.Schedule<int>(1ms, [] { return 0; }, nullptr);

    // Then it is rejected
    REQUIRE_FALSE(id.has_value());
  }

  SECTION("M call OnSuccess W void task returns") {
    // Given a scheduler with a task that returns nothing
    auto callback = std::make_shared<CountingCallback>();
    Scheduler scheduler;
    scheduler.Schedule<void>(1ms, [] {}, callback);

    // Then OnSuccess is called for every run and OnFailure never is
    REQUIRE(WaitFor([&] { return callback->successes >= 2; }));
    REQUIRE(callback->failures == 0);
  }

  SECTION("M report exception to OnFailure W void task throws") {
    // Given a scheduler with a void task that always throws
    auto callback = std::make_shared<CountingCallback>();
    Scheduler scheduler;
    scheduler.Schedule<void>(1ms, [] { throw std::runtime_error("boom"); }, callback);

    // Then every run reaches OnFailure and OnSuccess never runs
    REQUIRE(WaitFor([&] { return callback->failures >= 2; }));
    REQUIRE(callback->successes == 0);
  }
}
