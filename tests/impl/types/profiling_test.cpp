// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/types/profiling.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("ProfileEvent JSON serialization", "[unit][profiling]") {
  const UUID uuid_6ade = *UUID::Parse("6ade2e39-c3c4-42a9-93f5-d004e2cc452f");
  const UUID uuid_e294 = *UUID::Parse("e29459c7-2a4a-47da-93d9-2fd197c8a8f6");

  ProfileEvent event(
      Timestamp{std::chrono::milliseconds(1700000000000)},
      Timestamp{std::chrono::milliseconds(1700000060000)},
      "service:foo,env:bar",
      "windows",
      "windows",
      "4"
  );
  event.attachments = {"profile.pprof", "rum-mobile-events.json"};

  SECTION("M omit the RUM properties W there are none") {
    RequireJsonLiteral(
        event,
        R"({"start":"2023-11-14T22:13:20.000Z","end":"2023-11-14T22:14:20.000Z",)"
        R"("attachments":["profile.pprof","rum-mobile-events.json"],)"
        R"("tags_profiler":"service:foo,env:bar","family":"windows",)"
        R"("runtime":"windows","version":"4"})"
    );
  }

  SECTION("M render the RUM properties W they are set") {
    event.application_id = uuid_6ade;
    event.session_id = uuid_e294;
    event.view_id = uuid_6ade;
    event.view_name = "Home";
    event.action_id = uuid_e294;
    event.vital_id.value = {uuid_6ade, uuid_e294};
    event.vital_label.value = {"checkout", "upload"};
    event.long_task_id.value = {uuid_e294};

    RequireJsonObject(
        event,
        R"({"start":"2023-11-14T22:13:20.000Z","end":"2023-11-14T22:14:20.000Z",
            "attachments":["profile.pprof","rum-mobile-events.json"],
            "tags_profiler":"service:foo,env:bar","family":"windows",
            "runtime":"windows","version":"4",
            "application.id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f",
            "session.id":"e29459c7-2a4a-47da-93d9-2fd197c8a8f6",
            "view.id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f",
            "view.name":"Home",
            "user_action.id":"e29459c7-2a4a-47da-93d9-2fd197c8a8f6",
            "vital.id":["6ade2e39-c3c4-42a9-93f5-d004e2cc452f",
                        "e29459c7-2a4a-47da-93d9-2fd197c8a8f6"],
            "vital.label":["checkout","upload"],
            "long_task.id":["e29459c7-2a4a-47da-93d9-2fd197c8a8f6"]})"
    );
  }
}

TEST_CASE("ProfileRumEvent JSON serialization", "[unit][profiling]") {
  const UUID id = *UUID::Parse("6ade2e39-c3c4-42a9-93f5-d004e2cc452f");

  const Timestamp start{std::chrono::nanoseconds(1700000000123456789)};

  SECTION("M render a completed vital") {
    ProfileRumEvent event(id, ProfileRumEventType::Vital, start);
    event.name = "checkout";
    event.duration_ns = 400000000;

    RequireJsonLiteral(
        event,
        R"({"id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f","type":"vital",)"
        R"("name":"checkout","start_ns":1700000000123456789,"duration_ns":400000000})"
    );
  }

  SECTION("M omit the name and duration W there are none") {
    const ProfileRumEvent event(id, ProfileRumEventType::LongTask, start);

    RequireJsonLiteral(
        event,
        R"({"id":"6ade2e39-c3c4-42a9-93f5-d004e2cc452f","type":"long_task",)"
        R"("start_ns":1700000000123456789})"
    );
  }
}
