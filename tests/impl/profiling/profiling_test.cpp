// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profiling.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "datadog/impl/core/feature_message.hpp"
#include "datadog/impl/profiling/profile_frame.hpp"

#include "mock/profiler_backend.hpp"
#include "support/catch.hpp"
#include "support/context.hpp"
#include "support/core.hpp"
#include "support/feature.hpp"

using namespace datadog::impl;

TEST_CASE("Profiling storage config", "[unit][profiling]") {
  auto profiling = std::make_shared<impl::Profiling>(
      std::make_unique<MockProfilerBackend>(), ProfilerBackendSettings{}
  );
  const FeatureStorageConfig config = profiling->GetStorageConfig();

  // Each profile is stored in a batch file of its own, since an upload takes one
  // profile
  REQUIRE(config.max_writes_per_file == 1);

  // The limits leave room for a profile of 10 MB, which the default of 512 KB per event
  // would not, along with what is stored with it
  const size_t ten_mb = static_cast<size_t>(10) * 1024 * 1024;
  REQUIRE(config.max_event_size.has_value());
  REQUIRE(config.max_file_size.has_value());
  REQUIRE(*config.max_event_size > ten_mb);
  REQUIRE(*config.max_file_size >= *config.max_event_size);
}

namespace {

/** The mock Core context, with the given tracking consent. */
CoreContext MakeContext(datadog::TrackingConsent consent) {
  CoreContext context = MOCK_CONTEXT;
  context.tracking_consent = consent;
  return context;
}

/**
 * A Profiling feature that owns a mock profiler, and a Core context that starts with a
 * tracking consent. It has a harness that operates the feature as a running Core would,
 * and functions that change the consent as the Core does: one that delivers a context
 * update with a tracking consent while the Core runs, and one that changes it while the
 * Core is stopped.
 */
class ProfilingFixture {
 public:
  MockProfilerBackend& profiler;
  std::shared_ptr<impl::Profiling> profiling;
  CoreContext context;
  FeatureTest test;

  explicit ProfilingFixture(datadog::TrackingConsent initial_consent)
      : ProfilingFixture(std::make_unique<MockProfilerBackend>(), initial_consent) {}

  void Start() { test.Start(profiling); }

  void Stop() { test.Stop(profiling); }

  void SendConsent(datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    _handle_message(ContextChangedMessage{context});
  }

  // Like Core::SetTrackingConsent() when the Core is not running: the Core's context
  // changes at once, but the message bus is detached, so no message reaches the feature
  void ChangeConsentWhileStopped(datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    test.UpdateContext([consent](CoreContext& ctx) { ctx.tracking_consent = consent; });
  }

 private:
  // Keeps a reference to the profiler before the feature takes ownership of it
  ProfilingFixture(
      std::unique_ptr<MockProfilerBackend> mock_profiler,
      datadog::TrackingConsent initial_consent
  )
      : profiler(*mock_profiler),
        profiling(
            std::make_shared<impl::Profiling>(
                std::move(mock_profiler),
                ProfilerBackendSettings{"my-service", "staging", "1.2.3"}
            )
        ),
        context(MakeContext(initial_consent)),
        test(context),
        _handle_message(*profiling->MakeMessageHandler()) {}

  std::function<void(const FeatureMessage&)> _handle_message;
};

}  // namespace

TEST_CASE("Profiling feature lifecycle", "[unit][profiling]") {
  using datadog::TrackingConsent;

  SECTION("M configure and start the profiler W the core starts with consent granted") {
    ProfilingFixture f(TrackingConsent::Granted);

    // When the core starts
    f.Start();

    // Then the profiler is configured with the app's identity, then started
    REQUIRE(f.profiler.init_calls == 1);
    REQUIRE(f.profiler.start_calls == 1);
    REQUIRE(f.profiler.last_settings.service == "my-service");
    REQUIRE(f.profiler.last_settings.env == "staging");
    REQUIRE(f.profiler.last_settings.version == "1.2.3");
    REQUIRE(f.test.diagnostic_messages.empty());
  }

  SECTION("M start the profiler W the core starts with consent pending") {
    // Given consent that is pending: the profiles are held, until consent is granted,
    // by the storage that they are written to
    ProfilingFixture f(TrackingConsent::Pending);

    f.Start();

    REQUIRE(f.profiler.init_calls == 1);
    REQUIRE(f.profiler.start_calls == 1);
    REQUIRE(f.test.diagnostic_messages.empty());
  }

  SECTION("M not start the profiler W the core starts with consent not granted") {
    ProfilingFixture f(TrackingConsent::NotGranted);

    f.Start();

    // Then the profiler is never touched, including when the core stops
    f.Stop();
    REQUIRE(f.profiler.init_calls == 0);
    REQUIRE(f.profiler.start_calls == 0);
    REQUIRE(f.profiler.stop_calls == 0);
  }

  SECTION("M start the profiler W consent is granted after the core starts") {
    ProfilingFixture f(TrackingConsent::NotGranted);
    f.Start();
    REQUIRE(f.profiler.start_calls == 0);

    // When consent is granted
    f.SendConsent(TrackingConsent::Granted);

    // Then the profiler is configured and started
    REQUIRE(f.profiler.init_calls == 1);
    REQUIRE(f.profiler.start_calls == 1);
  }

  SECTION("M keep the profiler running W consent changes between pending and granted") {
    ProfilingFixture f(TrackingConsent::Pending);

    // When consent is granted, then pending again
    f.Start();
    f.SendConsent(TrackingConsent::Granted);
    f.SendConsent(TrackingConsent::Pending);

    // Then the profiler is set up and started once, and never stopped
    REQUIRE(f.profiler.init_calls == 1);
    REQUIRE(f.profiler.start_calls == 1);
    REQUIRE(f.profiler.stop_calls == 0);
  }

  SECTION("M stop the profiler W consent is withdrawn") {
    ProfilingFixture f(TrackingConsent::Granted);

    // When consent is revoked
    f.Start();
    f.SendConsent(TrackingConsent::NotGranted);

    // Then the profiler is stopped
    REQUIRE(f.profiler.stop_calls == 1);
  }

  SECTION("M stop the profiler W consent is refused while pending") {
    ProfilingFixture f(TrackingConsent::Pending);

    f.Start();
    f.SendConsent(TrackingConsent::NotGranted);

    REQUIRE(f.profiler.stop_calls == 1);
  }

  SECTION("M restart the profiler W consent is granted again") {
    ProfilingFixture f(TrackingConsent::Granted);

    // When consent is revoked, then granted again
    f.Start();
    f.SendConsent(TrackingConsent::NotGranted);
    f.SendConsent(TrackingConsent::Granted);

    // Then the profiler has been set up and started twice, and stopped once
    REQUIRE(f.profiler.init_calls == 2);
    REQUIRE(f.profiler.start_calls == 2);
    REQUIRE(f.profiler.stop_calls == 1);
  }

  SECTION("M stop the profiler W the core stops") {
    ProfilingFixture f(TrackingConsent::Granted);

    f.Start();
    f.Stop();

    REQUIRE(f.profiler.stop_calls == 1);
  }

  SECTION("M not start the profiler W consent arrives after the core stops") {
    // When a late context update arrives after the core has stopped
    ProfilingFixture f(TrackingConsent::NotGranted);
    f.Start();
    f.Stop();
    f.SendConsent(TrackingConsent::Granted);

    // Then the profiler is not started
    REQUIRE(f.profiler.init_calls == 0);
    REQUIRE(f.profiler.start_calls == 0);
  }

  SECTION("M start and stop the profiler for each run W the core is restarted") {
    ProfilingFixture f(TrackingConsent::Granted);

    // When the core runs twice
    f.Start();
    f.Stop();
    f.Start();
    f.Stop();

    // Then the profiler has been set up, started, and stopped once per run
    REQUIRE(f.profiler.init_calls == 2);
    REQUIRE(f.profiler.start_calls == 2);
    REQUIRE(f.profiler.stop_calls == 2);
    REQUIRE(f.test.diagnostic_messages.empty());
  }

  SECTION("M start the profiler W the core is restarted with consent unchanged") {
    ProfilingFixture f(TrackingConsent::Granted);

    // When the core is restarted
    f.Start();
    f.Stop();
    f.Start();

    // Then the profiler starts again at once, with the consent that the core has
    REQUIRE(f.profiler.start_calls == 2);
  }

  SECTION("M not start the profiler W consent was refused while the core was stopped") {
    ProfilingFixture f(TrackingConsent::Granted);

    // When consent is refused after the core stops, which the feature is not told of
    // because the message bus is detached, and the core is started again
    f.Start();
    f.Stop();
    f.ChangeConsentWhileStopped(TrackingConsent::NotGranted);
    f.Start();

    // Then the profiler is not started for the second run, not even briefly
    REQUIRE(f.profiler.start_calls == 1);
    REQUIRE(f.profiler.stop_calls == 1);
  }

  SECTION(
      "M not start the profiler W consent was refused before the core first started"
  ) {
    ProfilingFixture f(TrackingConsent::Granted);

    // When consent is refused after the feature is created, but before the core starts
    f.ChangeConsentWhileStopped(TrackingConsent::NotGranted);
    f.Start();

    // Then the profiler is never touched
    REQUIRE(f.profiler.init_calls == 0);
    REQUIRE(f.profiler.start_calls == 0);
  }

  SECTION("M start the profiler W consent was granted while the core was stopped") {
    ProfilingFixture f(TrackingConsent::NotGranted);

    // When consent is granted before the core starts
    f.ChangeConsentWhileStopped(TrackingConsent::Granted);
    f.Start();

    // Then the profiler starts with the run, without waiting for a context update
    REQUIRE(f.profiler.start_calls == 1);
  }

  SECTION("M warn and not start W init fails") {
    ProfilingFixture f(TrackingConsent::Granted);
    f.profiler.init_result = false;

    // When the profiler can't be configured
    f.Start();

    // Then a warning is logged and the profiler is not started
    REQUIRE(f.profiler.start_calls == 0);
    REQUIRE(f.test.diagnostic_messages.size() == 1);
    REQUIRE(f.test.diagnostic_messages[0].level == datadog::DiagnosticLevel::Warning);
    REQUIRE(
        f.test.diagnostic_messages[0].text ==
        "Profiling is not running: the profiler could not be configured, so no "
        "profiles "
        "will be collected or uploaded"
    );

    // And a later stop doesn't touch a profiler that never started
    f.Stop();
    REQUIRE(f.profiler.stop_calls == 0);
  }

  SECTION("M warn and not stop W start fails") {
    ProfilingFixture f(TrackingConsent::Granted);
    f.profiler.start_result = false;

    // When the profiler can't be started
    f.Start();
    f.Stop();

    // Then a warning is logged and the profiler is never stopped
    REQUIRE(f.profiler.init_calls == 1);
    REQUIRE(f.profiler.start_calls == 1);
    REQUIRE(f.test.diagnostic_messages.size() == 1);
    REQUIRE(f.test.diagnostic_messages[0].level == datadog::DiagnosticLevel::Warning);
    REQUIRE(
        f.test.diagnostic_messages[0].text ==
        "Profiling is not running: the profiler could not be started, so no profiles "
        "will be collected or uploaded"
    );
    REQUIRE(f.profiler.stop_calls == 0);
  }

  SECTION(
      "M not retry the profiler W unrelated context updates follow a failed start"
  ) {
    ProfilingFixture f(TrackingConsent::Granted);
    f.profiler.start_result = false;

    // When a start fails, then further context updates arrive with unchanged consent
    f.Start();
    f.SendConsent(TrackingConsent::Granted);
    f.SendConsent(TrackingConsent::Granted);
    f.SendConsent(TrackingConsent::Granted);

    // Then the start isn't retried and the warning isn't repeated
    REQUIRE(f.profiler.start_calls == 1);
    REQUIRE(f.test.diagnostic_messages.size() == 1);
  }
}

TEST_CASE("Profiling feature profile metadata", "[unit][profiling]") {
  // Given a Profiling feature that owns a mock profiler
  auto profiling = std::make_shared<impl::Profiling>(
      std::make_unique<MockProfilerBackend>(),
      ProfilerBackendSettings{"my-service", "staging", "1.2.3"}
  );
  auto message_handler_func = *profiling->MakeMessageHandler();

  // And helpers that deliver consent, and RUM events, as the Core and RUM do, with a
  // Core whose consent starts out refused
  CoreContext context = MakeContext(datadog::TrackingConsent::NotGranted);
  auto send_consent = [&](datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    message_handler_func(ContextChangedMessage{context});
  };
  const UUID long_task_id = UUID::Random();
  auto send_long_task = [&] {
    RumLongTaskGeneratedMessage message;
    message.context.view_id = UUID::Random();
    message.long_task_id = long_task_id;
    message.start = datadog::Timestamp{std::chrono::seconds{1700000000}};
    message.duration = std::chrono::milliseconds{60};
    message_handler_func(message);
  };
  const ProfileTimeRange range{
      datadog::Timestamp{std::chrono::seconds{1700000000}},
      datadog::Timestamp{std::chrono::seconds{1700000060}}
  };

  FeatureTest test(context);
  test.Start(profiling);

  SECTION("M describe the RUM events seen W the profiler is running") {
    // Given a running profiler that has seen a long task
    send_consent(datadog::TrackingConsent::Granted);
    send_long_task();

    // When the profiler asks for the metadata of a profile
    const ProfileMetadata metadata = profiling->BuildProfileMetadata(range);

    // Then it describes the application and the long task, and says that there were
    // RUM events
    REQUIRE(metadata.has_rum_events);
    const auto event = nlohmann::json::parse(metadata.event_json);
    REQUIRE(
        event["tags_profiler"].get<std::string>().find("service:mock-service") !=
        std::string::npos
    );
    REQUIRE(event["long_task.id"][0] == long_task_id.ToString());
    REQUIRE(nlohmann::json::parse(metadata.rum_events_json).size() == 1);
  }

  SECTION("M not record RUM events W the profiler is not running") {
    // When a long task is seen while consent is refused, so the profiler is not running
    send_consent(datadog::TrackingConsent::NotGranted);
    send_long_task();
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profile does not describe it, and says that there were no RUM events
    const ProfileMetadata metadata = profiling->BuildProfileMetadata(range);
    REQUIRE_FALSE(metadata.has_rum_events);
    REQUIRE_FALSE(nlohmann::json::parse(metadata.event_json).contains("long_task.id"));
  }

  SECTION("M forget the RUM events seen W the profiler stops") {
    // Given a running profiler that has seen a long task
    send_consent(datadog::TrackingConsent::Granted);
    send_long_task();

    // When consent is withdrawn, then granted again
    send_consent(datadog::TrackingConsent::NotGranted);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the long task is not described in the next profile
    const ProfileMetadata metadata = profiling->BuildProfileMetadata(range);
    REQUIRE_FALSE(nlohmann::json::parse(metadata.event_json).contains("long_task.id"));
  }
}

namespace {

datadog::Timestamp At(int64_t ms) {
  return datadog::Timestamp{
      std::chrono::duration_cast<datadog::Duration>(std::chrono::milliseconds{ms})
  };
}

/** A profile as the profiler delivers it, covering the minute after 1700000000000. */
ProfileData MakeProfile(std::vector<uint8_t> pprof = {1, 2, 3, 4}) {
  ProfileData profile;
  profile.pprof = std::move(pprof);
  profile.start = At(1700000000000);
  profile.end = At(1700000060000);
  return profile;
}

/** What is logged, at debug level, when a profile is dropped as unusable. */
const std::string DROPPED_PROFILE_MESSAGE =
    "Dropped a profile because it was missing, empty, or too large; profiling "
    "continues";

/** What is logged, at debug level, when a profile has no RUM events to correlate it. */
const std::string NO_RUM_EVENTS_MESSAGE =
    "Dropped a profile because there were no RUM events to correlate it with; "
    "profiling continues";

/** Reports a long task to the feature, as RUM does, for a profile to be correlated
 * with. */
void SendLongTask(const std::function<void(const FeatureMessage&)>& handle_message) {
  RumLongTaskGeneratedMessage message;
  message.context.view_id = UUID::Random();
  message.long_task_id = UUID::Random();
  message.start = datadog::Timestamp{std::chrono::seconds{1700000000}};
  message.duration = std::chrono::milliseconds{60};
  handle_message(message);
}

/** A vital that has completed, as RUM reports it: here, the time to initial display. */
RumVitalGeneratedMessage MakeVital() {
  return RumVitalGeneratedMessage{
      RumEventContext{},
      UUID::Random(),
      "time_to_initial_display",
      std::nullopt,
      std::nullopt,
      At(1700000000000),
      std::chrono::duration_cast<datadog::Duration>(std::chrono::milliseconds{500})
  };
}

}  // namespace

TEST_CASE("Profiling profile collection", "[unit][profiling]") {
  // Given a Profiling feature that owns a mock profiler
  auto mock_profiler = std::make_unique<MockProfilerBackend>();
  MockProfilerBackend& profiler = *mock_profiler;
  auto profiling = std::make_shared<impl::Profiling>(
      std::move(mock_profiler),
      ProfilerBackendSettings{"my-service", "staging", "1.2.3"}
  );
  auto message_handler_func = *profiling->MakeMessageHandler();

  // And helpers that deliver consent, and a long task, as the Core and RUM do, with a
  // Core whose consent starts out granted
  CoreContext context = MakeContext(datadog::TrackingConsent::Granted);
  auto send_consent = [&](datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    message_handler_func(ContextChangedMessage{context});
  };
  const UUID long_task_id = UUID::Random();
  auto send_long_task = [&] {
    RumLongTaskGeneratedMessage message;
    message.context.view_id = UUID::Random();
    message.long_task_id = long_task_id;
    message.start = datadog::Timestamp{std::chrono::seconds{1700000000}};
    message.duration = std::chrono::milliseconds{60};
    message_handler_func(message);
  };

  // And a function that lists the text of the messages logged so far at a level. The
  // text of a message ends with its attributes, as JSON, after a space: leave those
  // off.
  FeatureTest test(context);
  auto messages = [&](datadog::DiagnosticLevel level) {
    std::vector<std::string> texts;
    for (const auto& message : test.diagnostic_messages) {
      if (message.level == level) {
        texts.push_back(message.text.substr(0, message.text.find(" {")));
      }
    }
    return texts;
  };

  SECTION("M give the profiler its callback before configuring it W the core starts") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    REQUIRE(profiler.set_callback_calls == 1);
    REQUIRE(profiler.init_calls == 1);
    REQUIRE(profiler.callback_set_at_init);
  }

  SECTION("M not run the profiler W it cannot be given its callback") {
    // Given a profiler that cannot take a callback
    profiler.set_callback_result = false;

    // When the core starts and consent is granted
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profiler is not configured or started, since it has nowhere to deliver
    // profiles, and a warning says what that means for the application
    REQUIRE(profiler.init_calls == 0);
    REQUIRE(profiler.start_calls == 0);
    REQUIRE(
        messages(datadog::DiagnosticLevel::Warning) ==
        std::vector<std::string>{
            "Profiling is not running: the profiler could not be set up to deliver "
            "profiles to the SDK, so no profiles will be collected or uploaded"
        }
    );
  }

  SECTION("M store the profile as one event W the profiler delivers it") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // When the profiler delivers a profile, after RUM has reported a long task for it
    // to be correlated with
    send_long_task();
    REQUIRE(profiler.DeliverProfile(MakeProfile()));

    // Then it is stored as a single event, with no separate metadata block
    REQUIRE(test.events.size() == 1);
    REQUIRE(test.events[0].metadata.empty());

    // And it is stored according to the consent in effect, like any other feature's
    // events: held while consent is pending, available for upload once it is granted
    REQUIRE_FALSE(test.events[0].bypass_tracking_consent);

    // And the event holds the profile, with the event JSON that describes it
    const auto frame = DecodeProfileFrame(test.events[0].data);
    REQUIRE(frame.has_value());
    REQUIRE(frame->pprof == std::string_view("\x01\x02\x03\x04", 4));
    const auto event = nlohmann::json::parse(frame->event_json);
    REQUIRE(event["start"] == "2023-11-14T22:13:20.000Z");
    REQUIRE(event["end"] == "2023-11-14T22:14:20.000Z");
    REQUIRE(event["family"] == "windows");
    REQUIRE(
        event["tags_profiler"].get<std::string>().find("service:mock-service") !=
        std::string::npos
    );
    REQUIRE(test.diagnostic_messages.empty());
  }

  SECTION("M describe the RUM events seen W a profile is stored") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    send_long_task();

    REQUIRE(profiler.DeliverProfile(MakeProfile()));

    // Then the profile's metadata describes the long task
    REQUIRE(test.events.size() == 1);
    const auto frame = DecodeProfileFrame(test.events[0].data);
    REQUIRE(frame.has_value());
    const auto event = nlohmann::json::parse(frame->event_json);
    REQUIRE(event["long_task.id"][0] == long_task_id.ToString());
    REQUIRE(nlohmann::json::parse(frame->rum_events_json).size() == 1);
  }

  SECTION("M store each profile W the profiler delivers several") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Each profile needs a RUM event of its own: a profile takes the events seen so far
    send_long_task();
    REQUIRE(profiler.DeliverProfile(MakeProfile({1})));
    send_long_task();
    REQUIRE(profiler.DeliverProfile(MakeProfile({2, 2})));

    REQUIRE(test.events.size() == 2);
    const auto first = DecodeProfileFrame(test.events[0].data);
    const auto second = DecodeProfileFrame(test.events[1].data);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    REQUIRE(first->pprof == std::string_view("\x01", 1));
    REQUIRE(second->pprof == std::string_view("\x02\x02", 2));
  }

  SECTION("M drop the profile and log W the profiler reports an error") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    ProfileData failed;
    failed.error = "profile bytes unavailable";
    REQUIRE(profiler.DeliverProfile(std::move(failed)));

    // Then nothing is stored, and the profile's loss is logged for debugging, not as a
    // warning: profiling continues, and there is nothing for the application to fix
    REQUIRE(test.events.empty());
    REQUIRE(
        messages(datadog::DiagnosticLevel::Debug) ==
        std::vector<std::string>{DROPPED_PROFILE_MESSAGE}
    );
    REQUIRE(messages(datadog::DiagnosticLevel::Warning).empty());

    // And the message's attributes hold the reason
    REQUIRE(
        test.diagnostic_messages.back().text.find("profile bytes unavailable") !=
        std::string::npos
    );
  }

  SECTION("M drop the profile and log W the profile is empty") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    REQUIRE(profiler.DeliverProfile(MakeProfile({})));

    REQUIRE(test.events.empty());
    REQUIRE(
        messages(datadog::DiagnosticLevel::Debug) ==
        std::vector<std::string>{DROPPED_PROFILE_MESSAGE}
    );
    REQUIRE(messages(datadog::DiagnosticLevel::Warning).empty());

    // And the message's attributes hold the profile's size
    REQUIRE(
        test.diagnostic_messages.back().text.find("\"profile_size\":0") !=
        std::string::npos
    );
  }

  SECTION("M drop the profile and log W the profile is over 10 MB") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    const size_t ten_mb = static_cast<size_t>(10) * 1024 * 1024;

    // When the profiler delivers a profile one byte over the limit
    REQUIRE(profiler.DeliverProfile(MakeProfile(std::vector<uint8_t>(ten_mb + 1, 7))));

    // Then it is dropped
    REQUIRE(test.events.empty());
    REQUIRE(
        messages(datadog::DiagnosticLevel::Debug) ==
        std::vector<std::string>{DROPPED_PROFILE_MESSAGE}
    );
    REQUIRE(messages(datadog::DiagnosticLevel::Warning).empty());

    // And the message's attributes hold the profile's size, and the most allowed
    const std::string& text = test.diagnostic_messages.back().text;
    REQUIRE(text.find(std::to_string(ten_mb + 1)) != std::string::npos);
    REQUIRE(text.find(std::to_string(ten_mb)) != std::string::npos);
  }

  SECTION("M store the profile W it is exactly 10 MB") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    const size_t ten_mb = static_cast<size_t>(10) * 1024 * 1024;

    send_long_task();
    REQUIRE(profiler.DeliverProfile(MakeProfile(std::vector<uint8_t>(ten_mb, 7))));

    REQUIRE(test.events.size() == 1);
    const auto frame = DecodeProfileFrame(test.events[0].data);
    REQUIRE(frame.has_value());
    REQUIRE(frame->pprof.size() == ten_mb);
    REQUIRE(test.diagnostic_messages.empty());
  }

  SECTION("M forget the RUM events of a dropped profile W the next one has none") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Given a long task seen during a profile that the profiler fails to produce
    send_long_task();
    ProfileData failed;
    failed.error = "serialization failed";
    REQUIRE(profiler.DeliverProfile(std::move(failed)));

    // When the next profile is delivered, with no RUM events of its own
    REQUIRE(profiler.DeliverProfile(MakeProfile()));

    // Then the long task was forgotten, and not carried over to it: with nothing to
    // correlate it with, it is dropped as well
    REQUIRE(test.events.empty());
    REQUIRE(messages(datadog::DiagnosticLevel::Debug).back() == NO_RUM_EVENTS_MESSAGE);
  }

  SECTION("M drop the profile and log W there are no RUM events to correlate it with") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // When the profiler delivers a profile and RUM has reported nothing
    REQUIRE(profiler.DeliverProfile(MakeProfile()));

    // Then it is not kept, and the reason is logged for debugging, not as a warning:
    // profiling continues, and there is nothing for the application to fix
    REQUIRE(test.events.empty());
    REQUIRE(
        messages(datadog::DiagnosticLevel::Debug) ==
        std::vector<std::string>{NO_RUM_EVENTS_MESSAGE}
    );
    REQUIRE(messages(datadog::DiagnosticLevel::Warning).empty());
  }

  SECTION("M store the profile W RUM reported a vital") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // When RUM reports a vital, and then the profiler delivers a profile
    message_handler_func(MakeVital());
    REQUIRE(profiler.DeliverProfile(MakeProfile()));

    // Then the profile is kept, with the vital to correlate it with
    REQUIRE(test.events.size() == 1);
    const auto frame = DecodeProfileFrame(test.events[0].data);
    REQUIRE(frame.has_value());
    REQUIRE(nlohmann::json::parse(frame->event_json).contains("vital.id"));
  }

  SECTION("M drop the next profile W the one before took the RUM events") {
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // When one long task is followed by two profiles
    send_long_task();
    REQUIRE(profiler.DeliverProfile(MakeProfile({1})));
    REQUIRE(profiler.DeliverProfile(MakeProfile({2})));

    // Then only the first has the long task to correlate it with, so only it is kept
    REQUIRE(test.events.size() == 1);
    const auto frame = DecodeProfileFrame(test.events[0].data);
    REQUIRE(frame.has_value());
    REQUIRE(frame->pprof == std::string_view("\x01", 1));
  }

  SECTION("M do nothing W a profile arrives after the core stops") {
    // Given a core that has started, and then stopped
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);
    test.Stop(profiling);

    // When a profile is delivered anyway, which the feature cannot store
    REQUIRE(profiler.DeliverProfile(MakeProfile()));

    // Then nothing is stored, and nothing goes wrong
    REQUIRE(test.events.empty());
  }
}

TEST_CASE("Profiling shutdown", "[unit][profiling]") {
  // Given a Profiling feature that owns a mock profiler, and a running core
  auto mock_profiler = std::make_unique<MockProfilerBackend>();
  MockProfilerBackend& profiler = *mock_profiler;
  auto profiling = std::make_shared<impl::Profiling>(
      std::move(mock_profiler),
      ProfilerBackendSettings{"my-service", "staging", "1.2.3"}
  );
  auto message_handler_func = *profiling->MakeMessageHandler();
  CoreContext context = MakeContext(datadog::TrackingConsent::Granted);
  auto send_consent = [&](datadog::TrackingConsent consent) {
    context.tracking_consent = consent;
    message_handler_func(ContextChangedMessage{context});
  };
  FeatureTest test(context);
  test.Start(profiling);
  send_consent(datadog::TrackingConsent::Granted);
  REQUIRE(profiler.start_calls == 1);

  SECTION("M store the last profile W the core stops") {
    // Given a long task, for the last profile to be correlated with, and a profiler
    // that delivers that profile as it stops, from inside Stop()
    SendLongTask(message_handler_func);
    profiler.profile_on_stop = MakeProfile({9, 9, 9});

    // When the core stops
    test.Stop(profiling);

    // Then the profiler was stopped once, and the last profile was stored
    REQUIRE(profiler.stop_calls == 1);
    REQUIRE(test.events.size() == 1);
    const auto frame = DecodeProfileFrame(test.events[0].data);
    REQUIRE(frame.has_value());
    REQUIRE(frame->pprof == std::string_view("\x09\x09\x09", 3));
  }

  SECTION("M stop the profiler W the core is about to stop") {
    // When the feature is told that the core is about to stop, which is before it is
    // stopped
    profiling->OnCoreStopRequested();

    // Then the profiler is already stopped
    REQUIRE(profiler.stop_calls == 1);
  }

  SECTION("M not restart the profiler W consent is granted as the core stops") {
    // Given a feature that has been told that the core is about to stop
    profiling->OnCoreStopRequested();

    // When context updates arrive before the message bus stops, including one that
    // grants consent again
    send_consent(datadog::TrackingConsent::Pending);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profiler stays stopped
    REQUIRE(profiler.start_calls == 1);
    REQUIRE(profiler.stop_calls == 1);
  }

  SECTION("M run the profiler again W the core starts again after stopping") {
    // Given a core that stopped
    test.Stop(profiling);

    // When it starts again, and consent is granted
    test.Start(profiling);
    send_consent(datadog::TrackingConsent::Granted);

    // Then the profiler is given its callback again, and starts again
    REQUIRE(profiler.set_callback_calls == 2);
    REQUIRE(profiler.start_calls == 2);
  }
}

TEST_CASE("Profiling storage location", "[unit][profiling]") {
  const std::string pending_dir = "app/.datadog/main/12345/profiling/intermediate-v1";
  const std::string granted_dir = "app/.datadog/main/12345/profiling/v1";

  // A profile whose bytes are easy to find in the stored file
  const std::string marker = "profile-bytes-for-the-storage-test";
  const std::vector<uint8_t> pprof(marker.begin(), marker.end());

  // A function that runs a real Core, with the given tracking consent, and the
  // Profiling feature registered with it, and has the profiler deliver a profile while
  // the core is running. It stops the core, so that what the storage thread wrote is on
  // the filesystem, then passes the harness to `check` for examining it. (The harness
  // cannot be returned: it cannot be copied or moved.)
  auto run_core_with_profile = [&](datadog::TrackingConsent consent,
                                   const auto& check) {
    const bool flush_http_requests = false;
    CoreTestHarness test =
        CoreTestHarness::Init(MOCK_CORE_CONFIG, consent, flush_http_requests);
    auto mock_profiler = std::make_unique<MockProfilerBackend>();
    MockProfilerBackend& profiler = *mock_profiler;
    auto profiling = std::make_shared<impl::Profiling>(
        std::move(mock_profiler),
        ProfilerBackendSettings{"my-service", "staging", "1.2.3"}
    );
    REQUIRE(test.core.RegisterFeature(profiling));
    REQUIRE(test.core.Start());

    // RUM reports a long task for the profile to be correlated with. It is only
    // recorded while the profiler runs, which it does unless consent is not granted.
    SendLongTask(*profiling->MakeMessageHandler());
    REQUIRE(profiler.DeliverProfile(MakeProfile(pprof)));
    test.core.Stop();
    check(test);
  };

  SECTION("M hold the profile in the pending directory W consent is pending") {
    run_core_with_profile(
        datadog::TrackingConsent::Pending, [&](CoreTestHarness& test) {
          // Then the profile is stored where events wait for consent, as one file, and
          // is not yet available for upload
          auto names = test.fs.Ls(pending_dir);
          REQUIRE(names.size() == 1);
          REQUIRE(
              test.fs.Cat(pending_dir + "/" + names.front()).find(marker) !=
              std::string::npos
          );
          REQUIRE(test.fs.Ls(granted_dir).empty());
        }
    );
  }

  SECTION("M store the profile where it can be uploaded W consent is granted") {
    run_core_with_profile(
        datadog::TrackingConsent::Granted, [&](CoreTestHarness& test) {
          auto names = test.fs.Ls(granted_dir);
          REQUIRE(names.size() == 1);
          REQUIRE(
              test.fs.Cat(granted_dir + "/" + names.front()).find(marker) !=
              std::string::npos
          );
          REQUIRE(test.fs.Ls(pending_dir).empty());
        }
    );
  }

  SECTION("M not store the profile W consent is not granted") {
    run_core_with_profile(
        datadog::TrackingConsent::NotGranted, [&](CoreTestHarness& test) {
          // Nothing is stored: the profiler does not run, so it has no RUM events to
          // correlate the profile with, and storage would drop it in any case
          REQUIRE(test.fs.Ls(pending_dir).empty());
          REQUIRE(test.fs.Ls(granted_dir).empty());
        }
    );
  }
}

TEST_CASE("Profiling consent with a running core", "[unit][profiling]") {
  // Given a real Core that starts with consent granted, and a Profiling feature
  // registered with it
  const bool flush_http_requests = false;
  CoreTestHarness test = CoreTestHarness::Init(
      MOCK_CORE_CONFIG, datadog::TrackingConsent::Granted, flush_http_requests
  );
  auto mock_profiler = std::make_unique<MockProfilerBackend>();
  MockProfilerBackend& profiler = *mock_profiler;
  auto profiling = std::make_shared<impl::Profiling>(
      std::move(mock_profiler),
      ProfilerBackendSettings{"my-service", "staging", "1.2.3"}
  );
  REQUIRE(test.core.RegisterFeature(profiling));

  SECTION("M not start the profiler W consent is refused before the core starts") {
    // When the app refuses consent while the core is stopped, then starts the core
    test.core.SetTrackingConsent(datadog::TrackingConsent::NotGranted);
    REQUIRE(test.core.Start());

    // Then the profiler never starts, not even until the first context update arrives
    test.core.Stop();
    REQUIRE(profiler.init_calls == 0);
    REQUIRE(profiler.start_calls == 0);
  }

  SECTION("M not start the profiler W consent is refused while the core is stopped") {
    // When the core runs, stops, and the app refuses consent before it runs again
    REQUIRE(test.core.Start());
    test.core.Stop();
    REQUIRE(profiler.start_calls == 1);
    test.core.SetTrackingConsent(datadog::TrackingConsent::NotGranted);
    REQUIRE(test.core.Start());
    test.core.Stop();

    // Then the profiler does not start for the second run
    REQUIRE(profiler.start_calls == 1);
  }
}
