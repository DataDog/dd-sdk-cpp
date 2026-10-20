// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/rum_event_tracker.hpp"

#include <chrono>
#include <optional>
#include <string>

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

namespace {

Timestamp At(int64_t ms) {
  return Timestamp{std::chrono::duration_cast<Duration>(std::chrono::milliseconds{ms})};
}

RumEventContext MakeContext(std::string_view view_name = "Home") {
  RumEventContext context;
  context.ids.application_id = UUID::Random();
  context.ids.session_id = UUID::Random();
  context.ids.view_id = UUID::Random();
  context.view_name = view_name;
  return context;
}

RumVitalGeneratedMessage MakeStep(
    RumVitalStepType step_type,
    int64_t at_ms,
    std::string_view name = "checkout",
    std::optional<std::string> operation_key = std::nullopt,
    RumEventContext context = MakeContext()
) {
  return RumVitalGeneratedMessage{
      std::move(context),
      UUID::Random(),
      std::string(name),
      std::move(operation_key),
      step_type,
      At(at_ms),
      std::nullopt
  };
}

RumVitalGeneratedMessage MakeLaunchVital(int64_t launch_ms, int64_t duration_ms) {
  return RumVitalGeneratedMessage{
      MakeContext(),
      UUID::Random(),
      "time_to_initial_display",
      std::nullopt,
      std::nullopt,
      At(launch_ms),
      std::chrono::duration_cast<Duration>(std::chrono::milliseconds{duration_ms})
  };
}

RumLongTaskGeneratedMessage MakeLongTask(int64_t start_ms, int64_t duration_ms) {
  return RumLongTaskGeneratedMessage{
      MakeContext(),
      UUID::Random(),
      At(start_ms),
      std::chrono::duration_cast<Duration>(std::chrono::milliseconds{duration_ms})
  };
}

}  // namespace

TEST_CASE("RumEventTracker", "[unit][profiling]") {
  RumEventTracker tracker;

  SECTION("M return an empty snapshot W nothing has been recorded") {
    const auto snapshot = tracker.TakeSnapshot();

    REQUIRE(snapshot.IsEmpty());
    REQUIRE(snapshot.context.ids.application_id == UUID::Zero);
    REQUIRE(snapshot.context.view_name.empty());
  }

  SECTION("M record a vital as it arrived W it is not an operation step") {
    // When an app launch vital arrives
    const auto vital = MakeLaunchVital(1000, 250);
    tracker.OnVital(vital);

    // Then the snapshot has the vital, with the duration it arrived with
    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE(snapshot.vitals[0].id == vital.vital_id);
    REQUIRE(snapshot.vitals[0].name == "time_to_initial_display");
    REQUIRE(snapshot.vitals[0].start == At(1000));
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(250));
    REQUIRE_FALSE(snapshot.vitals[0].is_operation);
  }

  SECTION("M give an operation a duration W its end arrives") {
    // When an operation starts, then ends 400ms later
    const auto start = MakeStep(RumVitalStepType::Start, 1000, "checkout", "op-1");
    tracker.OnVital(start);
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1400, "checkout", "op-1"));

    // Then there is one vital, with the ID and start time of the start step
    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE(snapshot.vitals[0].id == start.vital_id);
    REQUIRE(snapshot.vitals[0].start == At(1000));
    REQUIRE(snapshot.vitals[0].operation_key == "op-1");

    // And its duration is the time between the steps
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(400));
  }

  SECTION("M match operations by name and operation key") {
    // When two operations with the same name but different keys overlap
    const auto first = MakeStep(RumVitalStepType::Start, 1000, "upload", "a");
    const auto second = MakeStep(RumVitalStepType::Start, 1100, "upload", "b");
    tracker.OnVital(first);
    tracker.OnVital(second);
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1500, "upload", "b"));
    tracker.OnVital(MakeStep(RumVitalStepType::End, 2000, "upload", "a"));

    // Then each is completed by its own end
    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.vitals.size() == 2);
    REQUIRE(snapshot.vitals[0].id == first.vital_id);
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(1000));
    REQUIRE(snapshot.vitals[1].id == second.vital_id);
    REQUIRE(snapshot.vitals[1].duration == std::chrono::milliseconds(400));
  }

  SECTION("M ignore update and retry steps") {
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000));
    tracker.OnVital(MakeStep(RumVitalStepType::Update, 1100));
    tracker.OnVital(MakeStep(RumVitalStepType::Retry, 1200));

    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE_FALSE(snapshot.vitals[0].duration.has_value());
  }

  SECTION("M ignore an end W the operation was never started") {
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1400));

    REQUIRE(tracker.TakeSnapshot().IsEmpty());
  }

  SECTION("M restart an operation W it starts again before it ends") {
    // When an operation starts twice, then ends
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000));
    const auto restart = MakeStep(RumVitalStepType::Start, 1300);
    tracker.OnVital(restart);
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1500));

    // Then there is one vital, measured from the second start
    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE(snapshot.vitals[0].id == restart.vital_id);
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(200));
  }

  SECTION("M record long tasks as they arrive") {
    const auto first = MakeLongTask(1000, 60);
    const auto second = MakeLongTask(2000, 120);
    tracker.OnLongTask(first);
    tracker.OnLongTask(second);

    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.long_tasks.size() == 2);
    REQUIRE(snapshot.long_tasks[0].id == first.long_task_id);
    REQUIRE(snapshot.long_tasks[0].start == At(1000));
    REQUIRE(snapshot.long_tasks[0].duration == std::chrono::milliseconds(60));
    REQUIRE(snapshot.long_tasks[1].id == second.long_task_id);
  }

  SECTION("M report the context of the latest event") {
    // When events arrive from different RUM contexts
    tracker.OnLongTask(MakeLongTask(1000, 60));
    const auto latest = MakeLongTask(2000, 60);
    tracker.OnLongTask(latest);

    // Then the snapshot has the context of the last of them
    const auto snapshot = tracker.TakeSnapshot();
    REQUIRE(snapshot.context.ids.view_id == latest.context.ids.view_id);
    REQUIRE(snapshot.context.ids.session_id == latest.context.ids.session_id);
  }

  SECTION("M begin a new snapshot W one is taken") {
    // Given a recorded vital and long task
    tracker.OnVital(MakeLaunchVital(1000, 250));
    tracker.OnLongTask(MakeLongTask(1000, 60));
    REQUIRE_FALSE(tracker.TakeSnapshot().IsEmpty());

    // When another snapshot is taken straight away
    const auto snapshot = tracker.TakeSnapshot();

    // Then it is empty, and its context is clear
    REQUIRE(snapshot.IsEmpty());
    REQUIRE(snapshot.context.ids.view_id == UUID::Zero);
  }

  SECTION("M keep an operation tracked W a snapshot is taken before it ends") {
    // Given an operation that has started
    const auto start = MakeStep(RumVitalStepType::Start, 1000);
    tracker.OnVital(start);

    // When a snapshot is taken, then the operation ends
    const auto before_end = tracker.TakeSnapshot();
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1700));
    const auto after_end = tracker.TakeSnapshot();

    // Then the first snapshot reports it without a duration
    REQUIRE(before_end.vitals.size() == 1);
    REQUIRE_FALSE(before_end.vitals[0].duration.has_value());

    // And the second reports it again, completed
    REQUIRE(after_end.vitals.size() == 1);
    REQUIRE(after_end.vitals[0].id == start.vital_id);
    REQUIRE(after_end.vitals[0].duration == std::chrono::milliseconds(700));

    // And it is reported no more once complete
    REQUIRE(tracker.TakeSnapshot().IsEmpty());
  }

  SECTION("M forget everything W reset") {
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000));
    tracker.OnLongTask(MakeLongTask(1000, 60));

    tracker.Reset();
    // An end for an operation forgotten by the reset has nothing to complete
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1700));

    REQUIRE(tracker.TakeSnapshot().IsEmpty());
  }

  SECTION("M drop further events W a kind reaches its cap") {
    // When more long tasks arrive than the cap allows
    for (size_t i = 0; i < RumEventTracker::kMaxEventsPerKind + 10; i++) {
      tracker.OnLongTask(MakeLongTask(1000, 60));
    }

    // Then only those up to the cap are kept
    REQUIRE(
        tracker.TakeSnapshot().long_tasks.size() == RumEventTracker::kMaxEventsPerKind
    );
  }
}
