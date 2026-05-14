// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/view_event_mutation.hpp"

#include "datadog/impl/rum/crash_processing/view_event_parser.hpp"

#include "support/catch.hpp"
#include "support/json_serialization.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumViewEventMutation", "[unit][crash_reporting]") {
  SECTION(
      "M produce modified view event W provided with valid event and parse results"
  ) {
    // Given an ordinary RUM view event with a variety of fields set
    const Timestamp date{std::chrono::milliseconds(946684799999)};
    const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
    const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
    const RumSessionType session_type = RumSessionType::Synthetics;
    const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
    const std::string_view view_url = "my-view";
    const uint64_t view_time_spent = 42;
    const uint64_t view_action_count = 3;
    const uint64_t view_error_count = 9;
    const uint64_t view_resource_count = 7;
    const uint64_t internal_document_version = 99;
    RumViewEvent ev{
        date,
        application_id,
        session_id,
        session_type,
        view_id,
        view_url,
        view_time_spent,
        view_action_count,
        view_error_count,
        view_resource_count,
        internal_document_version
    };
    ev.build_version = "mock-build-version";
    ev.build_id = R"(mock-"build"-id)";
    ev.view.name = "My View 🪟";
    ev.view.is_active = true;
    ev.os.value.emplace("MockOS", "5.6.0", "5");
    ev.usr.value.emplace();
    ev.usr.value->id = "user-id";
    ev.usr.value->extra.InitObject(8);
    ev.usr.value->extra.SetObjectProperty("id", Attribute::Int(100));
    ev.usr.value->extra.SetObjectProperty("view", Attribute::Int(101));
    ev.usr.value->extra.SetObjectProperty("type", Attribute::String("view"));
    ev.context = Attribute::Object(8);
    ev.context.value.SetObjectProperty("id", Attribute::Int(200));

    // And a string containing the JSON payload encoded from that RumViewEvent
    std::vector<uint8_t> buf;
    EncodeJson(buf, ev);
    const std::string_view view_json{reinterpret_cast<char*>(buf.data()), buf.size()};
    INFO(view_json);

    // And a RumViewEventParser that has successfully parsed that JSON payload
    RumViewEventParser parser;
    REQUIRE(parser.Parse(view_json));

    // When we produce a mutated view event in response to a crash report
    uint64_t crash_timestamp_ms = 946684803000;
    std::string got = MutateViewEventForCrash(
        view_json, parser.spans, parser.values, crash_timestamp_ms
    );

    // Then the resulting string is a valid JSON object
    auto got_obj = nlohmann::json::parse(got);
    REQUIRE(got_obj.is_object());

    // And it is also a schema-compliant RUM View event that exactly matches our
    // original event, except for a few mutated fields which have the expected values
    RequireEventMatch(got_obj, DATADOG_RUM_EVENT_LITERAL(R"({
      "type": "view",
      "date": 946684802999,
      "build_version": "mock-build-version",
      "build_id": "mock-\"build\"-id",
      "os": {
        "name": "MockOS",
        "version": "5.6.0",
        "version_major": "5"
      },
      "usr": {
        "id": "user-id",
        "view": 101,
        "type": "view"
      },
      "application": {
        "id": "a991ca10-4004-4004-4004-beefbeefbeef"
      },
      "session": {
        "id": "5e551017-4114-4114-4114-beeeefbeeeef",
        "type": "synthetics"
      },
      "view": {
        "id": "141ee144-4224-4224-4224-beeeeeeeeeef",
        "url": "my-view",
        "name": "My View 🪟",
        "is_active": false,
        "time_spent": 42,
        "action": {"count": 3},
        "error": {"count": 10},
        "crash": {"count": 1},
        "resource": {"count": 7}
      },
      "context": {
        "id": 200
      },
      "_dd": {
        "format_version": 2,
        "document_version": 100
      }
    })"));
  }
}
