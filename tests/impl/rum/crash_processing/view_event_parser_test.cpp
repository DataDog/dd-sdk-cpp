// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
//
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2025-Present Datadog, Inc.

#include "datadog/impl/rum/crash_processing/view_event_parser.hpp"

#include <chrono>
#include <cinttypes>
#include <vector>

#include "datadog/timestamp.hpp"
#include "datadog/uuid.hpp"

#include "datadog/impl/core/feature_types/rum.hpp"
#include "datadog/impl/core/util/json.hpp"

#include "support/catch.hpp"

using namespace datadog;
using namespace datadog::impl;

TEST_CASE("RumViewEventParser", "[unit][crash_reporting]") {
  SECTION("parses a minimal RUM View event") {
    // Given a RumViewEvent with all required fields
    const Timestamp date{std::chrono::nanoseconds(946684799999999999)};
    const UUID application_id = *UUID::Parse("a991ca10-4004-4004-4004-beefbeefbeef");
    const UUID session_id = *UUID::Parse("5e551017-4114-4114-4114-beeeefbeeeef");
    const RumSessionType session_type = RumSessionType::User;
    const UUID view_id = *UUID::Parse("141ee144-4224-4224-4224-beeeeeeeeeef");
    const std::string_view view_url = "my-view";
    const uint64_t view_time_spent = 42;
    const uint64_t view_action_count = 3;
    const uint64_t view_error_count = 9;
    const uint64_t view_resource_count = 7;
    const uint64_t internal_document_version = 5;
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

    // And a variety of permutations of various other optional fields
    auto with_build_info = GENERATE(false, true);
    if (with_build_info) {
      ev.build_version = "mock-build-version";
      ev.build_id = R"(mock-"build"-id)";
    }

    auto with_session_replay = GENERATE(false, true);
    if (with_session_replay) {
      ev.session.has_replay = true;
    }

    auto view_name =
        GENERATE(as<std::string_view>(), "", "My View", "My\n\"View\"\\🙂\u0007!!");
    ev.view.name = view_name;

    auto view_is_active = GENERATE(false, true);
    ev.view.is_active = view_is_active;

    auto with_context = GENERATE(false, true);
    if (with_context) {
      ev.usr.value.emplace();
      ev.usr.value->id = "user-id";
      ev.usr.value->extra.InitObject(8);
      ev.usr.value->extra.SetObjectProperty("id", Attribute::Int(100));
      ev.usr.value->extra.SetObjectProperty("view", Attribute::Int(101));
      ev.usr.value->extra.SetObjectProperty("type", Attribute::String("view"));

      ev.context = Attribute::Object(8);
      ev.context.value.SetObjectProperty("id", Attribute::Int(200));
    }

    // And a string containing the JSON payload encoded from our RumViewEvent
    std::vector<uint8_t> buf;
    EncodeJson(buf, ev);
    const std::string_view view_json{reinterpret_cast<char*>(buf.data()), buf.size()};
    INFO(view_json);

    // When we pass that string to RumViewEventParser and attempt to parse it
    RumViewEventParser parser;
    const bool ok = parser.Parse(view_json);

    // Then parsing succeeds
    REQUIRE(ok);

    // And the parser identifies correct values for all of the fields that we need
    REQUIRE(
        parser.values.build_version ==
        std::string_view(with_build_info ? "mock-build-version" : "")
    );
    REQUIRE(
        parser.values.build_id ==
        std::string_view(with_build_info ? "mock-\"build\"-id" : "")
    );
    REQUIRE(parser.values.application_id == application_id);
    REQUIRE(parser.values.session_id == session_id);
    REQUIRE(parser.values.session_type == session_type);
    REQUIRE(parser.values.session_has_replay == with_session_replay);
    REQUIRE(parser.values.view_id == view_id);
    REQUIRE(parser.values.view_url == view_url);
    REQUIRE(parser.values.view_name == view_name);
    REQUIRE(parser.values.view_error_count == view_error_count);
    REQUIRE(parser.values.dd_format_version == 2);
    REQUIRE(parser.values.dd_document_version == internal_document_version);
  }
}
