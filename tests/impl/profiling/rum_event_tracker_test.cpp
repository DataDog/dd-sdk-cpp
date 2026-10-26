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

// When snapshots are taken, unless a test says otherwise. Every operation in these
// tests starts shortly before this, and so is recent.
const Timestamp SNAPSHOT_TIME = At(10'000);

RumEventContext MakeContext(std::string_view view_name = "Home") {
  RumEventContext context;
  context.ids.application_id = UUID::Random();
  context.ids.session_id = UUID::Random();
  context.ids.view_id = UUID::Random();
  context.view_name = view_name;
  return context;
}

// The context in which operation steps occur unless a test says otherwise: every step
// is in one RUM session, as the steps of an operation normally are
RumEventContext SameSessionContext() {
  static const RumEventContext context = MakeContext();
  return context;
}

RumVitalGeneratedMessage MakeStep(
    RumVitalStepType step_type,
    int64_t at_ms,
    std::string_view name = "checkout",
    std::optional<std::string> operation_key = std::nullopt,
    RumEventContext context = SameSessionContext()
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
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);

    REQUIRE(snapshot.IsEmpty());
    REQUIRE(snapshot.context.ids.application_id == UUID::Zero);
    REQUIRE(snapshot.context.view_name.empty());
  }

  SECTION("M record a vital as it arrived W it is not an operation step") {
    // When an app launch vital arrives
    const auto vital = MakeLaunchVital(1000, 250);
    tracker.OnVital(vital);

    // Then the snapshot has the vital, with the duration it arrived with
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
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
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
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
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
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

    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE_FALSE(snapshot.vitals[0].duration.has_value());
  }

  SECTION("M ignore an end W the operation was never started") {
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1400));

    REQUIRE(tracker.TakeSnapshot(SNAPSHOT_TIME).IsEmpty());
  }

  SECTION("M restart an operation W it starts again before it ends") {
    // When an operation starts twice, then ends
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000));
    const auto restart = MakeStep(RumVitalStepType::Start, 1300);
    tracker.OnVital(restart);
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1500));

    // Then there is one vital, measured from the second start
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE(snapshot.vitals[0].id == restart.vital_id);
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(200));
  }

  SECTION("M track a new run W an operation starts again after it ended") {
    // Given an operation that has started and ended
    const auto first = MakeStep(RumVitalStepType::Start, 1000, "checkout", "op-1");
    tracker.OnVital(first);
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1400, "checkout", "op-1"));

    // When the same operation starts again before a snapshot is taken
    const auto second = MakeStep(RumVitalStepType::Start, 2000, "checkout", "op-1");
    tracker.OnVital(second);

    // Then both runs are reported: the second does not replace the first
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.vitals.size() == 2);
    REQUIRE(snapshot.vitals[0].id == first.vital_id);
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(400));
    REQUIRE(snapshot.vitals[1].id == second.vital_id);
    REQUIRE_FALSE(snapshot.vitals[1].duration.has_value());
  }

  SECTION("M follow each run W operations span profiles and restart") {
    // Given an operation that starts during profile 1
    const auto first = MakeStep(RumVitalStepType::Start, 1000, "checkout", "op-1");
    tracker.OnVital(first);

    // When profile 1 ends, the operation is reported without a duration
    const auto profile_1 = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(profile_1.vitals.size() == 1);
    REQUIRE(profile_1.vitals[0].id == first.vital_id);
    REQUIRE_FALSE(profile_1.vitals[0].duration.has_value());

    // And during profile 2 it ends, and then the same operation starts again
    tracker.OnVital(MakeStep(RumVitalStepType::End, 5000, "checkout", "op-1"));
    const auto second = MakeStep(RumVitalStepType::Start, 6000, "checkout", "op-1");
    tracker.OnVital(second);

    // Then profile 2 reports the first run, now complete, and the second, still running
    const auto profile_2 = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(profile_2.vitals.size() == 2);
    REQUIRE(profile_2.vitals[0].id == first.vital_id);
    REQUIRE(profile_2.vitals[0].duration == std::chrono::milliseconds(4000));
    REQUIRE(profile_2.vitals[1].id == second.vital_id);
    REQUIRE_FALSE(profile_2.vitals[1].duration.has_value());

    // And profile 3 reports only the second run, which is still running
    const auto profile_3 = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(profile_3.vitals.size() == 1);
    REQUIRE(profile_3.vitals[0].id == second.vital_id);
    REQUIRE_FALSE(profile_3.vitals[0].duration.has_value());

    // And once the second run ends, profile 4 reports it complete, and nothing is left
    tracker.OnVital(MakeStep(RumVitalStepType::End, 9000, "checkout", "op-1"));
    const auto profile_4 = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(profile_4.vitals.size() == 1);
    REQUIRE(profile_4.vitals[0].id == second.vital_id);
    REQUIRE(profile_4.vitals[0].duration == std::chrono::milliseconds(3000));
    REQUIRE(tracker.TakeSnapshot(SNAPSHOT_TIME).IsEmpty());
  }

  SECTION("M forget an operation W it has not ended and none is recent") {
    // Given an operation that started, and never ends
    const auto abandoned = MakeStep(RumVitalStepType::Start, 1000, "upload", "a");
    tracker.OnVital(abandoned);

    // When a snapshot is taken more than a minute after it started
    const Timestamp late = At(1000 + 61'000);
    const auto first = tracker.TakeSnapshot(late);

    // Then it is reported this once, without a duration
    REQUIRE(first.vitals.size() == 1);
    REQUIRE(first.vitals[0].id == abandoned.vital_id);
    REQUIRE_FALSE(first.vitals[0].duration.has_value());

    // And is not reported again, nor is the context kept for it
    const auto second = tracker.TakeSnapshot(late + std::chrono::seconds(60));
    REQUIRE(second.IsEmpty());
    REQUIRE(second.context.ids.view_id == UUID::Zero);

    // And a late end has nothing to complete
    tracker.OnVital(MakeStep(RumVitalStepType::End, 200'000, "upload", "a"));
    REQUIRE(tracker.TakeSnapshot(At(200'000)).IsEmpty());
  }

  SECTION("M keep an operation W it started less than a minute before the snapshot") {
    const auto recent = MakeStep(RumVitalStepType::Start, 1000, "upload", "a");
    tracker.OnVital(recent);

    // When snapshots are taken just inside the cutoff, and then just outside it
    const Timestamp inside = At(1000 + 59'000);
    REQUIRE(tracker.TakeSnapshot(inside).vitals.size() == 1);
    REQUIRE(tracker.TakeSnapshot(inside).vitals.size() == 1);
    REQUIRE(tracker.TakeSnapshot(At(1000 + 60'000)).vitals.size() == 1);

    // Then it is reported once more once the cutoff has passed, and no more after
    REQUIRE(tracker.TakeSnapshot(At(1000 + 61'000)).IsEmpty());
  }

  SECTION("M keep every running operation W any one of them is recent") {
    // Given an old operation, and a recent one
    const auto old_op = MakeStep(RumVitalStepType::Start, 1000, "old", "a");
    const auto recent = MakeStep(RumVitalStepType::Start, 100'000, "new", "b");
    tracker.OnVital(old_op);
    tracker.OnVital(recent);

    // When a snapshot is taken, the old one more than a minute after its start
    const auto first = tracker.TakeSnapshot(At(110'000));
    REQUIRE(first.vitals.size() == 2);

    // Then both are still tracked afterwards, as in dd-sdk-ios
    const auto second = tracker.TakeSnapshot(At(110'000));
    REQUIRE(second.vitals.size() == 2);
    REQUIRE(second.vitals[0].id == old_op.vital_id);
    REQUIRE(second.vitals[1].id == recent.vital_id);
  }

  SECTION("M forget only operations W completed ones are reported once") {
    // Given a completed vital and an abandoned operation
    tracker.OnVital(MakeLaunchVital(500, 250));
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000, "upload", "a"));

    // When a snapshot is taken long after
    const auto first = tracker.TakeSnapshot(At(1000 + 120'000));

    // Then both are reported, and neither is reported again
    REQUIRE(first.vitals.size() == 2);
    REQUIRE(tracker.TakeSnapshot(At(1000 + 180'000)).IsEmpty());
  }

  SECTION("M not match an end in a later session W the start was in an earlier one") {
    // Given an operation that started in one RUM session
    RumEventContext first_session = MakeContext();
    const auto start =
        MakeStep(RumVitalStepType::Start, 1000, "checkout", "op-1", first_session);
    tracker.OnVital(start);

    // When the session closes, and an end for the same name and key arrives in a new
    // session (RUM publishes it, though it has no record of the start)
    RumEventContext second_session = MakeContext();
    tracker.OnVital(
        MakeStep(RumVitalStepType::End, 4000, "checkout", "op-1", second_session)
    );

    // Then the end completes nothing: the start has no duration, and keeps its own
    // session
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE(snapshot.vitals[0].id == start.vital_id);
    REQUIRE_FALSE(snapshot.vitals[0].duration.has_value());
    REQUIRE(snapshot.vitals[0].context.ids.session_id == first_session.ids.session_id);
  }

  SECTION("M track each session's run W the same operation starts in two sessions") {
    // Given the same operation, started in two sessions
    RumEventContext first_session = MakeContext();
    RumEventContext second_session = MakeContext();
    const auto first =
        MakeStep(RumVitalStepType::Start, 1000, "checkout", "op-1", first_session);
    const auto second =
        MakeStep(RumVitalStepType::Start, 2000, "checkout", "op-1", second_session);
    tracker.OnVital(first);
    tracker.OnVital(second);

    // When the operation ends in the second session
    tracker.OnVital(
        MakeStep(RumVitalStepType::End, 3000, "checkout", "op-1", second_session)
    );

    // Then only the second run is completed, and the first is not replaced
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.vitals.size() == 2);
    REQUIRE(snapshot.vitals[0].id == first.vital_id);
    REQUIRE_FALSE(snapshot.vitals[0].duration.has_value());
    REQUIRE(snapshot.vitals[1].id == second.vital_id);
    REQUIRE(snapshot.vitals[1].duration == std::chrono::milliseconds(1000));
  }

  SECTION("M keep the operation's own context W another event came after it") {
    // Given an operation that started in one context, followed by a long task in
    // another
    RumEventContext operation_context = MakeContext("Checkout");
    tracker.OnVital(
        MakeStep(RumVitalStepType::Start, 1000, "upload", "a", operation_context)
    );
    const auto long_task = MakeLongTask(2000, 60);
    tracker.OnLongTask(long_task);

    // When the first snapshot is taken, it has the latest event's context
    const auto first = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(first.context.ids.view_id == long_task.context.ids.view_id);
    REQUIRE(first.long_tasks.size() == 1);

    // Then the next, which holds only the operation, has the operation's own context
    const auto second = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(second.long_tasks.empty());
    REQUIRE(second.vitals.size() == 1);
    REQUIRE(second.context.ids.view_id == operation_context.ids.view_id);
    REQUIRE(second.context.ids.session_id == operation_context.ids.session_id);
    REQUIRE(second.context.view_name == "Checkout");
  }

  SECTION("M keep the context of the newest start W an operation restarts") {
    // Given operation A, which starts in one view, and operation B, which starts later
    // in another
    RumEventContext first_view = MakeContext("First");
    RumEventContext other_view = MakeContext("Other");
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000, "a", "1", first_view));
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 2000, "b", "2", other_view));

    // When A starts again, in the same session but a newer view. This replaces A, which
    // is now the most recent start, and so comes after B.
    RumEventContext newer_view = first_view;
    newer_view.ids.view_id = UUID::Random();
    newer_view.view_name = "Newer";
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 3000, "a", "1", newer_view));
    tracker.TakeSnapshot(SNAPSHOT_TIME);

    // Then the operations are listed in the order they started, with the restarted A
    // last, and the context kept for the next snapshot is that of the restarted A
    const auto next = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(next.vitals.size() == 2);
    REQUIRE(next.vitals[0].name == "b");
    REQUIRE(next.vitals[1].name == "a");
    REQUIRE(next.vitals[1].context.view_name == "Newer");
    REQUIRE(next.context.ids.view_id == newer_view.ids.view_id);
    REQUIRE(next.context.view_name == "Newer");
  }

  SECTION("M ignore a second end W the operation already ended") {
    // Given an operation that has started and ended
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000, "checkout", "op-1"));
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1400, "checkout", "op-1"));

    // When another end arrives for it
    tracker.OnVital(MakeStep(RumVitalStepType::End, 9000, "checkout", "op-1"));

    // Then its duration is unchanged
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.vitals.size() == 1);
    REQUIRE(snapshot.vitals[0].duration == std::chrono::milliseconds(400));
  }

  SECTION("M record long tasks as they arrive") {
    const auto first = MakeLongTask(1000, 60);
    const auto second = MakeLongTask(2000, 120);
    tracker.OnLongTask(first);
    tracker.OnLongTask(second);

    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
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
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(snapshot.context.ids.view_id == latest.context.ids.view_id);
    REQUIRE(snapshot.context.ids.session_id == latest.context.ids.session_id);
  }

  SECTION("M begin a new snapshot W one is taken") {
    // Given a recorded vital and long task
    tracker.OnVital(MakeLaunchVital(1000, 250));
    tracker.OnLongTask(MakeLongTask(1000, 60));
    REQUIRE_FALSE(tracker.TakeSnapshot(SNAPSHOT_TIME).IsEmpty());

    // When another snapshot is taken straight away
    const auto snapshot = tracker.TakeSnapshot(SNAPSHOT_TIME);

    // Then it is empty, and its context is clear
    REQUIRE(snapshot.IsEmpty());
    REQUIRE(snapshot.context.ids.view_id == UUID::Zero);
  }

  SECTION("M keep an operation tracked W a snapshot is taken before it ends") {
    // Given an operation that has started
    const auto start = MakeStep(RumVitalStepType::Start, 1000);
    tracker.OnVital(start);

    // When a snapshot is taken, then the operation ends
    const auto before_end = tracker.TakeSnapshot(SNAPSHOT_TIME);
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1700));
    const auto after_end = tracker.TakeSnapshot(SNAPSHOT_TIME);

    // Then the first snapshot reports it without a duration
    REQUIRE(before_end.vitals.size() == 1);
    REQUIRE_FALSE(before_end.vitals[0].duration.has_value());

    // And the second reports it again, completed
    REQUIRE(after_end.vitals.size() == 1);
    REQUIRE(after_end.vitals[0].id == start.vital_id);
    REQUIRE(after_end.vitals[0].duration == std::chrono::milliseconds(700));

    // And it is reported no more once complete
    REQUIRE(tracker.TakeSnapshot(SNAPSHOT_TIME).IsEmpty());
  }

  SECTION("M keep the context W an operation is still waiting for its end") {
    // Given an operation that has started, in a known RUM context
    const auto start = MakeStep(RumVitalStepType::Start, 1000);
    tracker.OnVital(start);

    // When snapshots are taken with nothing else happening in between
    const auto first = tracker.TakeSnapshot(SNAPSHOT_TIME);
    const auto second = tracker.TakeSnapshot(SNAPSHOT_TIME);

    // Then each reports the operation in the context in which it started
    REQUIRE(first.context.ids.view_id == start.context.ids.view_id);
    REQUIRE(second.vitals.size() == 1);
    REQUIRE(second.context.ids.view_id == start.context.ids.view_id);
    REQUIRE(second.context.ids.session_id == start.context.ids.session_id);
    REQUIRE(second.context.view_name == start.context.view_name);

    // And once the operation ends and is reported, the context is released
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1700));
    tracker.TakeSnapshot(SNAPSHOT_TIME);
    REQUIRE(tracker.TakeSnapshot(SNAPSHOT_TIME).context.ids.view_id == UUID::Zero);
  }

  SECTION("M forget everything W reset") {
    tracker.OnVital(MakeStep(RumVitalStepType::Start, 1000));
    tracker.OnLongTask(MakeLongTask(1000, 60));

    tracker.Reset();
    // An end for an operation forgotten by the reset has nothing to complete
    tracker.OnVital(MakeStep(RumVitalStepType::End, 1700));

    REQUIRE(tracker.TakeSnapshot(SNAPSHOT_TIME).IsEmpty());
  }

  SECTION("M drop further events W a kind reaches its cap") {
    // When more long tasks arrive than the cap allows
    for (size_t i = 0; i < RumEventTracker::kMaxEventsPerKind + 10; i++) {
      tracker.OnLongTask(MakeLongTask(1000, 60));
    }

    // Then only those up to the cap are kept
    REQUIRE(
        tracker.TakeSnapshot(SNAPSHOT_TIME).long_tasks.size() ==
        RumEventTracker::kMaxEventsPerKind
    );
  }
}
