// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/profiling/profile_metadata.hpp"

#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "datadog/impl/core/platform/system_info.hpp"

#include "support/catch.hpp"
#include "support/context.hpp"

using namespace datadog;
using namespace datadog::impl;
using nlohmann::json;

namespace {

Timestamp At(int64_t ms) {
  return Timestamp{std::chrono::duration_cast<Duration>(std::chrono::milliseconds{ms})};
}

Duration Ms(int64_t ms) {
  return std::chrono::duration_cast<Duration>(std::chrono::milliseconds{ms});
}

// 2023-11-14T22:13:20.000Z, and 60 seconds later
const ProfileTimeRange RANGE{At(1700000000000), At(1700000060000)};

const platform::OsInfo OS_INFO{"Windows", "10.0.22631", "22631", "10"};
const platform::OsInfo OS_INFO_NO_VERSION{"Windows", "", "", ""};

/**
 * A CoreContext, together with what it views: the Core builds its identifying values,
 * including the sanitized unified service tags, from the configuration an application
 * provides.
 */
class TestContext {
 public:
  TestContext(
      std::string_view service,
      std::string_view env,
      std::string_view application_version,
      const platform::OsInfo& os
  )
      : _config(MakeConfig(service, env, application_version)),
        _immutable(_config, os, MOCK_DEVICE_INFO, At(1700000000000), "mock", "0.0.0"),
        _context(_immutable, TrackingConsent::Granted) {}

  TestContext(const TestContext&) = delete;
  TestContext& operator=(const TestContext&) = delete;

  const CoreContext& Get() const { return _context; }

 private:
  static CoreConfig MakeConfig(
      std::string_view service, std::string_view env, std::string_view version
  ) {
    CoreConfig config{"token", service, env};
    config.SetVersion(version);
    config.Internal_SetSource("cpp").Internal_SetSdkVersion("9.9.9");
    return config;
  }

  CoreConfig _config;
  ImmutableContext _immutable;
  CoreContext _context;
};

/** Returns the context of a typical application. */
const CoreContext& MakeContext() {
  static const TestContext typical("my-service", "staging", "1.2.3", OS_INFO);
  return typical.Get();
}

/** Builds the event.json content as text, with the tags that `context` yields. */
std::string EventJson(
    const CoreContext& context,
    const ProfileTimeRange& range,
    const RumEventSnapshot& events,
    ProfilingOperation operation = ProfilingOperation::Continuous
) {
  const ProfileTags tags = ProfileTags::FromContext(context);
  std::vector<uint8_t> buffer;
  BuildProfileEventJson(buffer, tags.Get(operation), range, events);
  return std::string(buffer.begin(), buffer.end());
}

/** Builds the rum-mobile-events.json content as text. */
std::string RumEventsJson(const RumEventSnapshot& events) {
  std::vector<uint8_t> buffer;
  BuildRumEventsJson(buffer, events);
  return std::string(buffer.begin(), buffer.end());
}

/** Views JSON text held in a buffer as a string. */
std::string_view Text(const std::vector<uint8_t>& buffer) {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return {reinterpret_cast<const char*>(buffer.data()), buffer.size()};
}

}  // namespace

TEST_CASE("BuildProfileEventJson", "[unit][profiling]") {
  const RumEventSnapshot no_events;

  SECTION("M describe the profile W no RUM events were seen") {
    const std::string result = EventJson(MakeContext(), RANGE, no_events);

    // Then the whole event is as follows, in the order iOS uses
    REQUIRE(
        result ==
        R"({"start":"2023-11-14T22:13:20.000Z","end":"2023-11-14T22:14:20.000Z",)"
        R"("attachments":["profile.pprof","rum-mobile-events.json"],)"
        R"("tags_profiler":"service:my-service,version:1.2.3,env:staging,)"
        R"(sdk_version:9.9.9,profiler_version:9.9.9,runtime_version:10.0.22631,)"
        R"(source:cpp,language:cpp,format:pprof,remote_symbols:yes,)"
        R"(operation:continuous",)"
        R"("family":"windows","runtime":"windows","version":4})"
    );
  }

  SECTION("M leave out tags with no value") {
    // Given an application with no version, running on an OS with no version
    const TestContext context("my-service", "staging", "", OS_INFO_NO_VERSION);

    const json event = json::parse(EventJson(context.Get(), RANGE, no_events));

    // Then those tags are left out, with no stray commas
    REQUIRE(
        event["tags_profiler"] ==
        "service:my-service,env:staging,sdk_version:9.9.9,profiler_version:9.9.9,"
        "source:cpp,language:cpp,format:pprof,remote_symbols:yes,operation:continuous"
    );
  }

  SECTION("M strip commas and colons from application-provided tag values") {
    // Given an application whose service, env, and version contain the characters that
    // separate tags and their values
    const TestContext context("my,service:x", "a:b", "1,2:3", OS_INFO);

    const json event = json::parse(EventJson(context.Get(), RANGE, no_events));

    // Then they cannot alter the structure of the tags, as with the tags that the Core
    // gives to RUM and Logs
    REQUIRE(
        event["tags_profiler"] ==
        "service:myservicex,version:123,env:ab,sdk_version:9.9.9,"
        "profiler_version:9.9.9,runtime_version:10.0.22631,source:cpp,language:cpp,"
        "format:pprof,remote_symbols:yes,operation:continuous"
    );
  }

  SECTION("M name the operation W it is not continuous") {
    const CoreContext context = MakeContext();

    const json launch = json::parse(
        EventJson(context, RANGE, no_events, ProfilingOperation::ApplicationLaunch)
    );
    const json custom =
        json::parse(EventJson(context, RANGE, no_events, ProfilingOperation::Custom));

    REQUIRE(
        std::string(launch["tags_profiler"]).find("operation:application_launch") !=
        std::string::npos
    );
    REQUIRE(
        std::string(custom["tags_profiler"]).find("operation:custom") !=
        std::string::npos
    );
  }

  SECTION("M carry the RUM IDs W events were seen") {
    // Given a vital, a long task, and the application and session of the latest of them
    RumEventSnapshot events;
    events.context.application_id = UUID::Random();
    events.context.session_id = UUID::Random();
    const UUID first_vital = UUID::Random();
    const UUID second_vital = UUID::Random();
    const UUID long_task = UUID::Random();
    events.vitals.push_back({first_vital, "checkout", "op-1", At(1), true, Ms(5), {}});
    events.vitals.push_back(
        {second_vital, "time_to_initial_display", std::nullopt, At(2), false, Ms(7), {}}
    );
    events.long_tasks.push_back({long_task, At(3), Ms(60)});

    // And two views that they took place in
    const UUID first_view = UUID::Random();
    const UUID second_view = UUID::Random();
    events.views.push_back({first_view, "Home"});
    events.views.push_back({second_view, "Checkout \"Page\""});

    // When the event is built
    const json event = json::parse(EventJson(MakeContext(), RANGE, events));

    // Then it identifies the application and session
    REQUIRE(event["application.id"] == events.context.application_id.ToString());
    REQUIRE(event["session.id"] == events.context.session_id.ToString());

    // And lists every view as an array, as the profile event schema does
    REQUIRE(
        event["view.id"] == json::array({first_view.ToString(), second_view.ToString()})
    );
    REQUIRE(event["view.name"] == json::array({"Home", "Checkout \"Page\""}));

    // And never identifies an action: the mobile profile event has none
    REQUIRE_FALSE(event.contains("user_action.id"));

    // And lists the vitals and long tasks
    REQUIRE(
        event["vital.id"] ==
        json::array({first_vital.ToString(), second_vital.ToString()})
    );
    REQUIRE(
        event["vital.label"] == json::array({"checkout", "time_to_initial_display"})
    );
    REQUIRE(event["long_task.id"] == json::array({long_task.ToString()}));
  }

  SECTION("M give only the views that have one a name") {
    // Given a view with a name and a view without
    RumEventSnapshot events;
    const UUID named = UUID::Random();
    const UUID unnamed = UUID::Random();
    events.views.push_back({named, "Home"});
    events.views.push_back({unnamed, ""});

    const json event = json::parse(EventJson(MakeContext(), RANGE, events));

    // Then both views are listed, but only one name
    REQUIRE(event["view.id"] == json::array({named.ToString(), unnamed.ToString()}));
    REQUIRE(event["view.name"] == json::array({"Home"}));
  }

  SECTION("M send the event version as a number") {
    const json event = json::parse(EventJson(MakeContext(), RANGE, RumEventSnapshot{}));

    REQUIRE(event["version"].is_number_integer());
    REQUIRE(event["version"] == 4);
  }

  SECTION("M leave out IDs W they are zero or there are none") {
    // Given a context with only an application and session
    RumEventSnapshot events;
    events.context.application_id = UUID::Random();
    events.context.session_id = UUID::Random();

    const json event = json::parse(EventJson(MakeContext(), RANGE, events));

    REQUIRE(event.contains("application.id"));
    REQUIRE(event.contains("session.id"));
    REQUIRE_FALSE(event.contains("view.id"));
    REQUIRE_FALSE(event.contains("view.name"));
    REQUIRE_FALSE(event.contains("user_action.id"));
    REQUIRE_FALSE(event.contains("vital.id"));
    REQUIRE_FALSE(event.contains("vital.label"));
    REQUIRE_FALSE(event.contains("long_task.id"));
  }
}

TEST_CASE("BuildRumEventsJson", "[unit][profiling]") {
  SECTION("M build an empty array W no events were seen") {
    REQUIRE(RumEventsJson(RumEventSnapshot{}) == "[]");
  }

  SECTION("M describe each vital and long task") {
    // Given a completed operation, an unfinished one, and a long task
    RumEventSnapshot events;
    const UUID done = UUID::Random();
    const UUID open = UUID::Random();
    const UUID long_task = UUID::Random();
    events.vitals.push_back({done, "checkout", "op-1", At(1000), true, Ms(400), {}});
    events.vitals.push_back(
        {open, "upload", std::nullopt, At(2000), true, std::nullopt, {}}
    );
    events.long_tasks.push_back({long_task, At(3000), Ms(60)});

    const json result = json::parse(RumEventsJson(events));

    // Then each is described with its ID, type, and start in epoch nanoseconds
    REQUIRE(result.size() == 3);
    REQUIRE(result[0]["id"] == done.ToString());
    REQUIRE(result[0]["type"] == "vital");
    REQUIRE(result[0]["name"] == "checkout");
    REQUIRE(result[0]["start_ns"] == 1000 * 1000000LL);
    REQUIRE(result[0]["duration_ns"] == 400 * 1000000LL);

    // And an unfinished operation has no duration
    REQUIRE(result[1]["id"] == open.ToString());
    REQUIRE(result[1]["name"] == "upload");
    REQUIRE_FALSE(result[1].contains("duration_ns"));

    // And a long task has no name
    REQUIRE(result[2]["id"] == long_task.ToString());
    REQUIRE(result[2]["type"] == "long_task");
    REQUIRE_FALSE(result[2].contains("name"));
    REQUIRE(result[2]["start_ns"] == 3000 * 1000000LL);
    REQUIRE(result[2]["duration_ns"] == 60 * 1000000LL);
  }
}

TEST_CASE("ProfileMetadataSource", "[unit][profiling]") {
  auto source = std::make_shared<ProfileMetadataSource>();
  source->OnContextChanged(MOCK_CONTEXT);

  SECTION("M use the identity from the context W it was last updated") {
    const ProfileMetadata metadata = source->BuildMetadata(RANGE);

    const json event = json::parse(metadata.event_json);
    const std::string tags = event["tags_profiler"];
    REQUIRE(tags.find("service:mock-service") != std::string::npos);
    REQUIRE(tags.find("env:mock-env") != std::string::npos);
    REQUIRE(tags.find("source:mock-source") != std::string::npos);
    REQUIRE(tags.find("sdk_version:1.2.3") != std::string::npos);
    REQUIRE(tags.find("runtime_version:2.3.4") != std::string::npos);
    REQUIRE(Text(metadata.rum_events_json) == "[]");
  }

  SECTION("M consume the RUM events W metadata is built") {
    // Given a long task
    RumLongTaskGeneratedMessage message;
    message.context.view_id = UUID::Random();
    message.long_task_id = UUID::Random();
    message.start = At(1000);
    message.duration = Ms(60);
    source->OnLongTask(message);

    // When metadata is built
    const ProfileMetadata first = source->BuildMetadata(RANGE);

    // Then it describes the long task
    REQUIRE(
        json::parse(first.event_json)["long_task.id"][0] ==
        message.long_task_id.ToString()
    );
    REQUIRE(json::parse(first.rum_events_json).size() == 1);

    // And the view it took place in, which has no name
    REQUIRE(json::parse(first.event_json)["view.id"].size() == 1);
    REQUIRE_FALSE(json::parse(first.event_json).contains("view.name"));

    // And the next profile does not
    const ProfileMetadata second = source->BuildMetadata(RANGE);
    REQUIRE_FALSE(json::parse(second.event_json).contains("long_task.id"));
    REQUIRE(Text(second.rum_events_json) == "[]");
  }
}
